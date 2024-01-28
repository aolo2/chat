function channel_scroller_filter(msg, message_id) {
    // NOTE(mk): this function should be called on older messages before it is called on newer ones
    if (!(msg.thread_id in filter_thread_cache) || filter_thread_cache[msg.thread_id] === message_id) {
        filter_thread_cache[msg.thread_id] = message_id
        return true;
    }
    return false;
}

/**
 * Message scroleler widget 
 * @param {Object} options
 * @param {HTMLElement} options.root
 * @param {function(string, Object)} options.handle_reply
 * @param {function(string, Object)} options.handle_edit
 * @param {boolean} options.in_thread
 * @constructor
 */
function Scroller(options) {
    // TODO: look into "init races": sometimes draw_chunk gets called before the context.source is set

    let scroller_context = scroller_create_context(null);
    let scroller_observer = null;
    let scroller_placeholder_messages = [];
    let scroller_spinner = null;
    let handle_reply = null;
    let handle_edit = null;
    let in_thread = false;
    let html_elements = {};

    function scroller_create_context(source, channel_id = null) {
        const result = {
            'channel_id': channel_id,
            'source': source,
            'filter': null,
            'enter_margin': 1500,
            'leave_margin': 2500,
            'indicator_enter_margin': 500,
            'indicator_leave_margin': 300,
            'from': 0,
            'to': 0,
            'height': 0,
            'working': false,
            'follow': true,
            'downthere': 0,
            'last_scrolltop': 0,
            'jumping': false,
            'send_seen_id': null,

            'channel_reset_timeout_id': null,

            'current_date_indicator': null,
            'current_date_hide_id': null,
            // 'scroll_active': false,
            // 'smoothscroll_duration': 500, // NOTE(aolo2): onscroll event throttling timeout
        };

        return result;
    }

    function init() {
        html_elements['root'] = options.root;
        handle_reply = options.handle_reply;
        handle_edit = options.handle_edit;
        in_thread = options.in_thread;
        render();
        const io_options = { root: null, rootMargin: '0px', threshold: 1.0 };
        scroller_observer = new IntersectionObserver(scroller_visible, io_options);
        scroller_spinner = spinner_init(html_elements['message-spinner'])
    }

    function render() {
        html_elements.root.innerHTML = get_inner_html();
        html_elements['message-container'] = html_elements.root.querySelector('.message-container');
        html_elements['godown'] = html_elements.root.querySelector('.godown');
        html_elements['spinner-container'] = html_elements.root.querySelector('.spinner-container');
        html_elements['message-spinner'] = html_elements.root.querySelector('.message-spinner');
        html_elements['hovering-date'] =  html_elements.root.querySelector('.hovering-date');

        html_elements['godown'].addEventListener('click', scroller_turn_on_follow_mode);
        html_elements['message-container'].addEventListener('scroll', onscroll, { passive: true });

    }

// NOTE(aolo2): if this triggers, but the websocket send
// doesn't succeed (we are offline/server is offline), then
// the data gets lost. We don't care. This is not critical info,
// the messages will get "seen" when you read the next one while online.
    function scroller_visible(entries) {
        let max_seen = 0;
        let seen_messages = false;

        for (const entry of entries) {
            const target = entry.target;
            if (target.classList.contains('message') && entry.intersectionRatio > 0.5) {
                const index = +target.getAttribute('data-index');
                if (index > max_seen) max_seen = index;
                seen_messages = true;
                scroller_observer.unobserve(entry.target);
            } else if (target.classList.contains('date-separator') && entry.intersectionRatio === 1.0) {
                const index = +target.getAttribute('data-before');

                if (index >= 0) {
                    scroller_context.current_date_indicator = scroller_context.source[index].timestamp;
                    scroller_show_current_date_indicator();
                }
            }
        }

        if (!seen_messages) {
            return;
        }

        const new_unseen = max_seen + 1;
        const channel = ls_get_channel(scroller_context.channel_id);

        if (!channel) {
            console.error('Scroller set visible for a non-existent channel');
            return;
        }

        channel.client.first_unseen = new_unseen;

        ls_set_channel(channel);
        redraw_channel(channel);
        websocket_send_seen(scroller_context.channel_id, new_unseen); // TODO(aolo2): do not send "seen" for own messages, can be derived from SYNs

        let still_have_unread = false;
        const channel_list = ls_get_channel_list();

        // TODO(aolo2): @speed ? We do this on every message observe. Can possibly kill scrolling
        for (const channel_id of channel_list) {
            const channel = ls_get_channel(channel_id);
            if (channel) {
                const last_real_message_index = last_nondeleted_message_index(channel.id);
                if (last_real_message_index !== null && channel.client.first_unseen <= last_real_message_index) {
                    still_have_unread = true;
                    break;
                }
            }
        }

        if (!still_have_unread) {
            notification_all_read();
        }
    }

    function scroller_show_current_date_indicator() {
        if (scroller_context.current_date_hide_id !== null) {
            clearTimeout(scroller_context.current_date_hide_id);
            scroller_context.current_date_hide_id = null;
        }

        const this_date = new Date(scroller_context.current_date_indicator * 1000);
        const date_string = this_date.getDate() + ' ' + month_string(this_date);

        html_elements['hovering-date'].innerText = date_string;
        html_elements['hovering-date'].classList.remove('hidden');

        scroller_context.current_date_hide_id = setTimeout(() => {
            html_elements['hovering-date'].classList.add('hidden');
        }, 2000);
    }

    function scroller_show_spinner() {
        scroller_spinner.start();
        html_elements['spinner-container'].classList.remove('dhide');
    }

    function scroller_hide_spinner() {
        scroller_spinner.stop();
        html_elements['spinner-container'].classList.add('dhide');
    }

    function scroller_set_channel(channel_id) {
        if (channel_id !== null) {
            if (scroller_context.channel_reset_timeout_id !== null) {
                clearTimeout(scroller_context.channel_reset_timeout_id);
                scroller_context.channel_reset_timeout_id = null;
            }

            if (scroller_context && channel_id === scroller_context.channel_id) {
                return;
            }

            scroller_show_spinner();
            scroller_context = scroller_create_context(messages[channel_id], channel_id);
            scroller_redraw(() => {
                html_elements['message-container'].scrollTo({ 'top': html_elements['message-container'].scrollHeight })
                scroller_hide_spinner();
            });
            scroller_establish_basepoint(messages[channel_id].length);

            for (const tmp_msg of scroller_placeholder_messages) {
                if (tmp_msg.channel_id === channel_id) {
                    scroller_append_tmp(tmp_msg.message, tmp_msg.key);
                }
            }
        } else {
            scroller_context = scroller_create_context(null);
        }
    }

    function scroller_set_filter(filter, on_complete = null) {
        const current_channel_id = ls_get_current_channel_id();
        if (filter === null || current_channel_id === null) {
            scroller_context.filter = null;
            scroller_redraw(on_complete);
            return;
        }
        const rset = new Set();

        for (let i = 0; i < messages[current_channel_id].length; ++i) {
            const msg = messages[current_channel_id][i];
            if (filter(msg, i)) {
                rset.add(i);
            }
            if (msg.type !== RECORD.MESSAGE && rset.has(msg.message_id)) {
                rset.add(i);
            }
        }

        scroller_context.filter = {
            'set': rset,
            'last_processed': messages[current_channel_id].length,
            'condition': filter
        };

        scroller_redraw(on_complete);
    }

    function scroller_establish_basepoint(point) {
        scroller_context.from = scroller_context.to = point;
    }

    function scroller_redraw(on_complete = null, on_each = null) {
        html_elements['message-container'].innerHTML = '';
        if (scroller_context.source === null) return;

        scroller_context.from = scroller_context.to = scroller_context.source ? scroller_context.source.length : 0;
        scroller_context.height = 0;
        scroller_context.follow = true;

        scroller_draw('up', () => {
            // TODO: html_elements['message-container'].scrollTop = scroller_context.last_scrolltop
            if (on_complete) on_complete();
        }, on_each);
    }

    function scroller_turn_on_follow_mode() {
        if (scroller_context.to < scroller_context.source.length) {
            scroller_redraw(() => {
                scroller_context.follow = true;
            }, () => html_elements['message-container'].scrollTop = html_elements['message-container'].scrollHeight);
        } else {
            scroller_context.follow = true;
            html_elements['message-container'].scrollTo({ 'top': html_elements['message-container'].scrollHeight });
        }
    }

    function onscroll() {
        // console.log('scroll, working =', scroller_context.working)
        // if (scroller_context.scroll_active) {
        //     return;
        // }

        // scroller_context.scroll_active = true;
        // setTimeout(() => {
        //     scroller_context.scroll_active = false;
        // }, scroller_context.smoothscroll_duration);

        const up = (html_elements['message-container'].scrollTop < scroller_context.last_scrolltop);
        const from_top = html_elements['message-container'].scrollTop;
        const from_bottom = html_elements['message-container'].scrollHeight - (html_elements['message-container'].scrollTop + html_elements['message-container'].clientHeight);
        scroller_context.last_scrolltop = html_elements['message-container'].scrollTop;

        if (from_bottom < scroller_context.indicator_leave_margin) {
            html_elements['godown'].classList.add('hidden');
            scroller_context.follow = true;
        } else {
            scroller_context.follow = false;
        }

        if (from_bottom > scroller_context.indicator_enter_margin) {
            html_elements['godown'].classList.remove('hidden');
        }

        if (up) {
            if (!scroller_context.working && from_top < scroller_context.enter_margin) {
                scroller_draw('up');
            }
        } else {
            if (!scroller_context.working && from_bottom < scroller_context.enter_margin) {
                scroller_draw('down');
            }
        }
    }

    function filtered(msg, index) {
        if (scroller_context.filter === null) return false;
        if (scroller_context.filter.last_processed - 1 >= index) return !scroller_context.filter.set.has(index);
        return !scroller_context.filter.condition(msg, index);
    }

    function previous_message(index) {
        let other_index = index - 1;

        while (other_index >= 0) {
            const other_message = scroller_context.source[other_index];

            if (other_message.type === RECORD.MESSAGE && !other_message.deleted && !filtered(other_message, other_index)) {
                break;
            }

            if ([RECORD.USER_LEFT, RECORD.USER_JOINED, RECORD.TITLE_CHANGED].includes(other_message.type) && !filtered(other_message, other_index)) {
                break;
            }

            --other_index;
        }

        return other_index;
    }

    function next_message(index) {
        let other_index = index + 1;

        while (other_index < scroller_context.source.length) {
            const other_message = scroller_context.source[other_index];
            if (other_message.type === RECORD.MESSAGE && !other_message.deleted && !filtered(other_message, other_index)) {
                break;
            }
            ++other_index;
        }

        if (other_index >= scroller_context.source.length) {
            other_index = -1;
        }

        return other_index;
    }

    function same_group(index) {
        const message = scroller_context.source[index];
        const other_index = previous_message(index);

        if (other_index < 0) {
            return false;
        }

        const other_message = scroller_context.source[other_index];

        if (message.author_id !== other_message.author_id) {
            return false;
        }

        if (message.timestamp - other_message.timestamp > 60) {
            return false;
        }

        return true;
    }

    function different_day(index) {
        const message = scroller_context.source[index];
        const other_index = previous_message(index);

        if (other_index < 0) {
            return true;
        }

        const other_message = scroller_context.source[other_index];

        const this_date = new Date(message.timestamp * 1000);
        const other_date = new Date(other_message.timestamp * 1000);

        return !same_day(this_date, other_date);
    }

    function draw_chunk(size, direction, on_complete, on_each) {
        const me = ls_get_me();
        const old_height = html_elements['message-container'].scrollHeight;
        let from_top = html_elements['message-container'].scrollTop;
        let from_bottom = html_elements['message-container'].scrollHeight - (html_elements['message-container'].scrollTop + html_elements['message-container'].clientHeight);

        if (direction === 'up' && (scroller_context.from === 0 || from_top >= scroller_context.leave_margin)) {
            scroller_context.working = false;
            // console.log('working end 1');
            if (on_complete) on_complete();
            return;
        } else if (direction === 'down' && (scroller_context.to === scroller_context.source.length || from_bottom >= scroller_context.leave_margin)) {
            scroller_context.working = false;
            // console.log('working end 1.1');
            if (on_complete) on_complete();
            return;
        }

        if (html_elements['message-container'].scrollTop === 0) {
            html_elements['message-container'].scrollTop = 1; // NOTE(aolo2): so that it doesn't auto-scroll up
        }

        // if (html_elements['message-container'].scrollTop === html_elements['message-container'].scrollHeight) {
        //     html_elements['message-container'].scrollTop = html_elements['message-container'].scrollHeight - 1; // NOTE(aolo2): so that it doesn't auto-scroll down
        // }

        const chunk = document.createElement("div");
        chunk.classList.add('message-chunk');

        let added = 0;
        while (added < size) {
            if (direction === 'up' && scroller_context.from === 0) {
                break;
            }

            if (direction === 'down' && scroller_context.to === scroller_context.source.length) {
                break;
            }

            const message_id = (direction === 'up' ? scroller_context.from - 1 : scroller_context.to);
            const message = scroller_context.source[message_id];
            const channel = ls_get_channel(scroller_context.channel_id);
            const is_regular = (message.type === RECORD.MESSAGE);

            if ([RECORD.MESSAGE, RECORD.USER_LEFT, RECORD.USER_JOINED, RECORD.TITLE_CHANGED].includes(message.type)) {
                if (!message.deleted && !filtered(message, message_id)) {
                    const item = create_item(message_id, scroller_context.channel_id, {
                        reply: handle_reply,
                        edit: handle_edit,
                        jump_to: scroller_jump_to
                    }, false, in_thread); // TODO: this should not return null ever
                    if (item) {
                        if (is_regular && search_really_on) {
                            make_clickable(item, () => {
                                search_toggle(() => scroller_jump_to(message_id));
                            });
                        }

                        if (is_regular && same_group(message_id)) {
                            item.classList.add('same-author');
                        }

                        const day_changed = different_day(message_id);
                        let separator = null;

                        if (day_changed) {
                            const this_date = new Date(message.timestamp * 1000);
                            const date_string = this_date.getDate() + ' ' + month_string(this_date);
                            const prev_index = previous_message(message_id);
                            separator = HTML(`
                            <div class="date-separator" data-before="${prev_index}" data-after="${message_id}">
                            <hr class="date-separator-hr">
                            <span class="date-separator-date">${date_string}</span>
                            </div>
                            `);
                            scroller_observer.observe(separator);
                        }

                        if (direction === 'up') {
                            chunk.prepend(item);
                            if (day_changed) chunk.prepend(separator);
                        } else {
                            if (day_changed) chunk.append(separator);
                            chunk.append(item);
                        }

                        if (is_regular && message_id >= channel.client.first_unseen) {
                            scroller_observer.observe(item);
                        }

                        ++added;
                    }
                }
            }

            if (direction === 'up') {
                scroller_context.from -= 1;
            } else {
                scroller_context.to += 1;
            }
        }

        if (chunk.childElementCount > 0) {
            if (direction === 'up') {
                html_elements['message-container'].prepend(chunk);
            } else {
                html_elements['message-container'].append(chunk);
            }
        }

        // overflow-anchor does this automagically
        // ...but is not supported in Safari(WebKit) :///

        if (!CSS.supports('overflow-anchor', 'auto')) {
            const dY = html_elements['message-container'].scrollHeight - scroller_context.height;
            if (direction === 'up') {
                html_elements['message-container'].scrollTop += dY;
            }
        }

        scroller_context.height = html_elements['message-container'].scrollHeight;
        if (on_each) on_each();

        // from_top/from_bottom will get checked at the start of next RAF
        if (direction === 'up' && scroller_context.from > 0 && from_top < scroller_context.leave_margin) {
            window.requestAnimationFrame(() => draw_chunk(size, 'up', on_complete, on_each));
        } else if (direction === 'down' && from_bottom < scroller_context.leave_margin) {
            window.requestAnimationFrame(() => draw_chunk(size, 'down', on_complete, on_each));
        } else {
            // console.log('working end 2');
            scroller_context.working = false;
            if (on_complete) on_complete();
        }
    }

    function scroller_draw(direction, on_complete, on_each = null) {
        const items_per_frame = 10;
        scroller_context.working = true;
        // console.log('working start');
        window.requestAnimationFrame(() => draw_chunk(items_per_frame, direction, on_complete, on_each));
    }

    function scroller_apply_decoration(channel_id, record) {
        if (channel_id !== scroller_context.channel_id) {
            return;
        }

        if (record.type === RECORD.MESSAGE) {
            let thread_id = record.thread_id;
            let in_same_thread = scroller_context.source.filter(r => r.thread_id === thread_id && !r.deleted);
            if (in_same_thread.length > 1 && !in_thread) {
                let first_of_thread = scroller_context.source.findIndex(r => r.thread_id === thread_id);
                let element = html_elements['message-container'].querySelector(`.message[data-index="${first_of_thread}"]`);
                if (element) {
                    set_thread_info(element, thread_id, in_same_thread.length);
                }
            }
        } else if (record.type === RECORD.DELETE) {
            let thread_id = scroller_context.source[record.message_id].thread_id;
            let in_same_thread = scroller_context.source.filter(r => r.thread_id === thread_id);
            if (in_same_thread.length > 1 && !in_thread) {
                let first_of_thread = scroller_context.source.findIndex(r => r.thread_id === thread_id);
                let element = html_elements['message-container'].querySelector(`.message[data-index="${first_of_thread}"]`);
                if (element) {
                    set_thread_info(element, thread_id, scroller_context.source.filter(r => r.thread_id === thread_id && !r.deleted).length);
                }
            }
        }

        if (scroller_context.from <= record.message_id && record.message_id < scroller_context.to) {
            const item = html_elements['message-container'].querySelector(`.message[data-index="${record.message_id}"]`);
            if (item) {
                switch (record.type) {
                    case RECORD.DELETE: {

                        // NOTE(aolo2): if we deleted the first message of a group, the next message should
                        // now become NOT 'same-author'
                        const next_index = next_message(record.message_id);
                        if (next_index >= 0 && scroller_context.source[record.message_id].author_id === scroller_context.source[next_index].author_id && !item.classList.contains('same-author')) {
                            const next_item = html_elements['message-container'].querySelector(`.message[data-index="${next_index}"]`);
                            if (next_item) {
                                next_item.classList.remove('same-author');
                            }
                        }

                        // NOTE(aolo2): preserve correct separator behaviour on message delete
                        const separator_below = html_elements['message-container'].querySelector(`.date-separator[data-before="${record.message_id}"]`);
                        const separator_above = html_elements['message-container'].querySelector(`.date-separator[data-after="${record.message_id}"]`);

                        if (separator_above) {
                            if (separator_below) {
                                const prev_index = previous_message(record.message_id);
                                separator_above.remove();
                                separator_below.setAttribute('data-before', prev_index);
                            } else if (next_index >= 0) {
                                separator_above.setAttribute('data-after', next_index);
                            } else {
                                separator_above.remove();
                            }
                        }

                        item.remove();

                        break;
                    }

                    case RECORD.EDIT: {
                        // NOTE(aolo2): could have already been applied (via local prediction)
                        const processed = process_message_text(record.text);

                        if (record.text.length > 0) {
                            item.querySelector('.text').classList.remove('dhide');
                            item.querySelector('.text').innerHTML = processed.text;
                        } else {
                            item.querySelector('.text').classList.add('dhide');
                        }

                        if (processed.emojied) {
                            item.classList.add('emojied');
                            item.querySelectorAll('.text .emoji16').forEach((e) => {
                                e.classList.remove('emoji16');
                                e.classList.add('emoji32');
                            });
                        } else {
                            item.classList.remove('emojied');
                            item.querySelectorAll('.text .emoji32').forEach((e) => {
                                e.classList.remove('emoji32');
                                e.classList.add('emoji16');
                            });
                        }

                        break;
                    }

                    case RECORD.REPLY: {
                        const first_line = scroller_context.source[record.reply_to].text.split('\n')[0];
                        const reply_text = process_message_text(first_line).text;
                        item.querySelector('.reply-preview').innerHTML = reply_text;
                        break;
                    }

                    case RECORD.REACTION_ADD: {
                        const message = scroller_context.source[record.message_id];
                        const emoji = emoji_get_by_code(record.reaction_id);
                        const reaction_count = message.reactions[record.reaction_id].length;
                        const reaction_authors = message.reactions[record.reaction_id].map(id => ls_find_user(id) ? ls_find_user(id).name : '').join(', ');

                        if (!emoji) break;

                        if (reaction_count === 1) {
                            const old_reaction_item = item.querySelector(`.reaction[data-code="${record.reaction_id}"]`);
                            if (!old_reaction_item) {
                                const emoji_size = (touch_device ? 32 : 16);
                                const reaction_item = HTML(`
                                <div class="reaction" data-code="${record.reaction_id}" title="${reaction_authors}">
                                    <div class="emoji${emoji_size} e${emoji.s}"></div>
                                    <span class="reaction-count">${reaction_count}</span>
                                </div>`);
                                item.querySelector('.reactions').appendChild(reaction_item);

                                make_clickable(reaction_item, () => {
                                    click_reaction(channel_id, record.message_id, record.reaction_id);
                                });
                            }
                        } else {
                            const reaction_item = item.querySelector(`.reaction[data-code="${record.reaction_id}"]`);
                            reaction_item.setAttribute('title', reaction_authors);
                            reaction_item.querySelector(`span.reaction-count`).innerHTML = reaction_count;
                        }

                        item.querySelector('.bot-row').classList.remove('dhide');

                        break;
                    }

                    case RECORD.REACTION_REMOVE: {
                        const message = scroller_context.source[record.message_id];
                        const emoji = emoji_get_by_code(record.reaction_id);
                        const reaction_count = message.reactions[record.reaction_id].length;
                        const reaction_authors = message.reactions[record.reaction_id].map(id => ls_find_user(id) ? ls_find_user(id).name : '').join(', ');

                        if (!emoji) break;

                        if (reaction_count === 0) {
                            const old_reaction_item = item.querySelector(`.reaction[data-code="${record.reaction_id}"]`);
                            if (old_reaction_item) {
                                old_reaction_item.remove();
                            }
                        } else {
                            const reaction_item = item.querySelector(`.reaction[data-code="${record.reaction_id}"]`);
                            reaction_item.setAttribute('title', reaction_authors);
                            reaction_item.querySelector(`span.reaction-count`).innerHTML = reaction_count;
                        }

                        let total_reaction_count = 0;
                        for (const key in message.reactions) {
                            total_reaction_count += message.reactions[key].length;
                        }

                        if (total_reaction_count === 0) {
                            item.querySelector('.bot-row').classList.add('dhide');
                        }

                        break;
                    }

                    case RECORD._LEGACY_USER_LEFT:
                    case RECORD._LEGACY_USER_JOINED:
                    case RECORD.USER_LEFT:
                    case RECORD.USER_JOINED:
                    case RECORD.TITLE_CHANGED:
                    case RECORD.PIN: {
                        break;
                    }

                    case RECORD.ATTACH: {
                        item.replaceWith(create_item(record.message_id, channel_id, {
                            reply: handle_reply,
                            edit: handle_edit,
                            jump_to: scroller_jump_to
                        }, false, in_thread));
                        break;
                    }

                    default: {
                        console.log('WARNING: unhandled decoration type', record.type);
                    }
                }
            }
        }
    }

    function scroller_append(channel_id, first_open) {
        if (channel_id !== scroller_context.channel_id) {
            return;
        }

        if (first_open) {
            scroller_draw('up');
        } else if (scroller_context.to < scroller_context.source.length) {
            scroller_draw('down', () => {
                if (scroller_context.follow) {
                    html_elements['message-container'].scrollTo({ 'top': html_elements['message-container'].scrollHeight });
                }
            });
        }
    }

    function scroller_jump_to(index) {
        if (scroller_context.from <= index && index < scroller_context.to) {
            const item = html_elements['message-container'].querySelector(`.message[data-index="${index}"]`);
            if (item) {
                scroller_context.jumping = true;
                item.scrollIntoView({ 'block': 'center', 'behaviour': 'smooth' });
                item.classList.add('highlight');
                setTimeout(() => {
                    if (item) item.classList.remove('highlight');
                    scroller_context.jumping = false;
                }, 2000);
            }
        } else {
            // TODO(aolo2): special draw_chunk('bidirectional') to draw around the jumped to message
            scroller_show_spinner();
            scroller_context.follow = false;
            scroller_context.from = scroller_context.to = Math.min(index + 1, scroller_context.source.length);
            html_elements['message-container'].innerHTML = '';
            scroller_context.height = 0;
            scroller_draw('up', () => {
                const item = html_elements['message-container'].querySelector(`.message[data-index="${index}"]`);
                if (item) {
                    item.classList.add('highlight');
                    setTimeout(() => {
                        if (item) item.classList.remove('highlight');
                    }, 2000);

                    scroller_draw('down', () => {
                        scroller_hide_spinner();
                        item.scrollIntoView({ 'block': 'center', 'behaviour': 'auto' });
                    });
                } else {
                    scroller_hide_spinner();
                }
            });
        }
    }

    function scroller_append_tmp(message, key) {
        const item = create_item(message, scroller_context.channel_id, { jump_to: scroller_jump_to }, true, in_thread);
        item.setAttribute('data-tmp-key', scroller_context.channel_id + '-' + key);
        html_elements['message-container'].append(item);
        html_elements['message-container'].scrollTo({ 'top': html_elements['message-container'].scrollHeight });
    }

    function scroller_add_tmp(message, key) {
        const placeholder_timer = setTimeout(() => {
            if (scroller_context.channel_id === message.channel_id) {
                scroller_append_tmp(message, key);
            }
        }, CONFIG_MESSAGE_PLACEHOLDER_DELAY);

        scroller_placeholder_messages.push({
                                               'channel_id': scroller_context.channel_id,
                                               'key': key,
                                               'message': message,
                                               'placeholder_timer': placeholder_timer
                                           });
    }

    function scroller_remove_tmp(channel_id, key) {
        let found_tmp = false;

        for (let i = 0; i < scroller_placeholder_messages.length; ++i) {
            const tmp_message = scroller_placeholder_messages[i];
            if (tmp_message.channel_id === channel_id && tmp_message.key === key) {
                clearTimeout(tmp_message.placeholder_timer);
                scroller_placeholder_messages.splice(i, 1);
                found_tmp = true;
                break;
            }
        }

        if (found_tmp && channel_id === scroller_context.channel_id) {
            const item = html_elements['message-container'].querySelector(`.message[data-tmp-key="${scroller_context.channel_id}-${key}"]`);
            if (item) {
                item.remove();
            }
        }
    }

    function scroller_set_seen(channel_id, first_unseen) {
        if (channel_id !== scroller_context.channel_id) {
            return;
        }

        for (let i = scroller_context.from; i < scroller_context.to; ++i) {
            if (i >= first_unseen) {
                break;
            }

            const item = html_elements['message-container'].querySelector(`.message[data-index="${i}"]`);
            if (item && item.classList.contains('acked')) {
                item.classList.remove('acked');
                item.classList.add('seen');
            }
        }
    }

    function highlight_message(index) {
        // Unhighlight the old message
        if (index === null || item_factory_highlight !== null) {
            const item = html_elements['message-container'].querySelector(`.message[data-index="${item_factory_highlight}"]`);
            if (item) {
                item.classList.remove('highlight');
            }
            item_factory_highlight = null;
        }

        // Highlight the new one 
        if (index !== null) {
            item_factory_highlight = index;
            const item = html_elements['message-container'].querySelector(`.message[data-index="${index}"]`);
            if (item) {
                item.classList.add('highlight');
            }
        }
    }

    function get_inner_html() {
        return `
        <div class="hovering-date-indicator-wrapper">
            <div class="hovering-date value hidden"></div>
        </div>
        <div class="spinner-container">
            <div class="loader">
                <svg width="64" height="64" xmlns="http://www.w3.org/2000/svg">
                    <path class="message-spinner" fill="none" stroke="#ddd" stroke-width="3" stroke-linecap="round"/>
                </svg>
            </div>
        </div>
        <div class="message-container" tabindex="1"></div>
        <div class="godown hidden">
            <img draggable="false" src="static/icons/down-arrow.svg">
        </div>
        `
    }

    function get_last_mine_message() {
        const me = ls_get_me();
        for (let i = scroller_context.source.length - 1; i >= 0; --i) {
            const message = scroller_context.source[i];
            if (message.type === RECORD.MESSAGE && message.author_id === me.user_id && !message.deleted && !filtered(message, i)) {
                return { i, message };
            }
        }

        return { i: -1, message: null };
    }

    this.set_channel = scroller_set_channel;
    this.set_filter = scroller_set_filter;
    this.context = scroller_context;
    this.turn_on_follow_mode = scroller_turn_on_follow_mode;
    this.append = scroller_append;
    this.apply_decoration = scroller_apply_decoration;
    this.jump_to = scroller_jump_to;
    this.add_tmp = scroller_add_tmp;
    this.remove_tmp = scroller_remove_tmp;
    this.establish_basepoint = scroller_establish_basepoint;
    this.set_seen = scroller_set_seen;
    this.get_last_mine_message = get_last_mine_message;
    this.init = init;
}

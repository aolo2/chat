let item_factory = null;
let item_factory_child = null;
let item_factory_avatar = null;
let item_factory_text = null;
let item_factory_attachments = null;
let item_factor_actions = null;
let item_factory_reactions = null;
let item_factory_author = null;
let item_factory_timestamp = null;
let item_factory_reply_author = null;
let item_factory_reply_text = null;
let item_factory_highlight = null;

function show_delete_popup(index) {
    popup_show('Delete message', 'Are you sure?', [
        {
            'class': 'negative',
            'text': 'delete',
            'action': () => enqueue_delete(ls_get_current_channel_id(), index)
        },
        {
            'text': 'cancel'
        }
    ]);
}

function ext_to_icon(ext) {
    switch (ext) {
        case 5: return 'audio.svg';
        case 6: return 'video.svg';
        case 10: return 'archive.svg';
    }
    return 'file.svg';
}

function create_item(index, channel_id, handlers, tmp = false, in_thread = false) {
    const me = ls_get_me();

    if (item_factory === null) {
        item_factory = find('item-factory');
        item_factory_child = document.createElement('div');
        item_factory_child.innerHTML = `
        <div class="overtop-row">
            <div class="reply-content">
                <div class="reply-author"></div>
                <div class="reply-preview"></div>
                <div class="reply-spacer"></div>
            </div>
            <div class="reply-angle"></div>
        </div>
        <div class="top-row">
            <div class="message-header">
                <span class="author"></span>
                <span class="time"></span>
            </div>
        </div>
        <div class="mid-row">
            <div class="avatar"></div>
            <div class="content">
                <div class="images"></div>
                <div class="text"></div>
                <div class="attachments"></div>
                <div class="indicator">
                    <img draggable="false" class="acked" src="static/icons/tick.svg">
                    <img draggable="false" class="seen" src="static/icons/tick2.svg">
                    <img draggable="false" class="failed" src="static/icons/failed.svg">
                </div>
            </div>
            <div class="actions">
            </div>
        </div>
        <div class="bot-row dhide">
            <div class="reactions"></div>
        </div>
        <div class="thread-row dhide">
        </div>`;

        item_factory_child.classList.add('message');
        item_factory.appendChild(item_factory_child);
        item_factory_avatar = item_factory_child.querySelector('.avatar');
        item_factory_images = item_factory_child.querySelector('.images');
        item_factory_text = item_factory_child.querySelector('.text');
        item_factory_attachments = item_factory_child.querySelector('.attachments');
        item_factory_reactions = item_factory_child.querySelector('.reactions');
        item_factory_author = item_factory_child.querySelector('.author');
        item_factor_actions = item_factory_child.querySelector('.actions');
        item_factory_timestamp = item_factory_child.querySelector('.time');
        item_factory_reply_author = item_factory_child.querySelector('.reply-author');
        item_factory_reply_text = item_factory_child.querySelector('.reply-preview');
    }

    const message = tmp ? index : messages[channel_id][index];

    if (message.type === RECORD.USER_LEFT || message.type === RECORD.USER_JOINED || message.type === RECORD.TITLE_CHANGED) {
        const user = ls_find_user(message.user_id) || {'name': 'Unknown user'};
        let result = null;
        if (message.type === RECORD.TITLE_CHANGED) {
            result = HTML(`<div class="system-message">
            ${user.name} changed the title to "${message.title}"
            </div>`);
        } else {
            const action = (message.type === RECORD.USER_LEFT ? 'left' : 'joined');
            result = HTML(`<div class="system-message">
            ${user.name} ${action} the channel
        </div>`);
        }

        return result;
    }

    const author_index = message.author_id;
    const author = ls_find_user(author_index);

    if (author === null) return null;

    item_factory_author.innerHTML = author.name;

    if (author_index !== me.user_id) {
        if (author.avatar !== '0') {
            const preview_level = (touch_device ? 2 : 3);
            item_factory_avatar.innerHTML = `<img draggable="false" src="/storage/${author.avatar}-${preview_level}">`;
        } else {
            item_factory_avatar.innerHTML = `<img draggable="false" src="/static/images/avatar_placeholder.svg">`;
        }
    }

    const message_date = new Date(message.timestamp * 1000);
    let hours = message_date.getHours();
    let minutes = message_date.getMinutes();

    if (hours < 10) hours = '0' + hours;
    if (minutes < 10) minutes = '0' + minutes;

    item_factory_timestamp.innerHTML = hours + ':' + minutes;
    item_factory_text.innerHTML = '';
    item_factory_images.innerHTML = '';

    let message_text = message.text;
    let has_images = false;
    let has_attachments = false;  

    let images = [];

    if ('attachments' in message) {
        // console.log(message.attachments)
        let attachment_html = '';

        if (message.attachments.length === 1 && message.attachments[0].ext === ATTACHMENT_TYPE.EXT_VIDEO) {
            const video = message.attachments[0];
            attachment_html = `<video controls>
                <source src="/storage/${video.id}" type="video/mp4" />
                Video not supported by browser. <a href="/storage/${video.id}">Download</a> instead?
            </video>`
            has_attachments = true;
        } else {
            for (const attachment of message.attachments) {
                if (is_supported_image(attachment.ext)) {
                    images.push(attachment);
                }
            }

            if (images.length === 1) {
                // One big boy image
                const last_image = images[0];
                item_factory_images.innerHTML = 
                `<div class="images">
                    <img style="width: ${last_image.width}px; height: ${last_image.height}px;" draggable="false" class="big-img" data-file-id="f-${last_image.id}">
                </div>`;  // Actual src is set lower down in wait_for_image_preview;
            } else if (images.length > 1) {
                let images_html = '<div class="image-grid">';

                for (const image of images) {
                    images_html += `<img draggable="false" class="med-img" data-file-id="f-${image.id}">`;  // Actual src is set lower down in wait_for_image_preview
                }
                
                images_html += '</div>';
                item_factory_images.innerHTML = images_html;
            }

            for (const attachment of message.attachments) {
                if (!is_supported_image(attachment.ext)) {
                    has_attachments = true;
                    const icon = ext_to_icon(attachment.ext);
                    attachment_html += `
                    <a target="_blank" download="${attachment.name}" href="/storage/${attachment.id}">
                        <div class="item">
                            <div class="icon">
                                <img draggable="false" src="static/icons/${icon}">
                            </div>
                            <span class="text">${attachment.name}</span>
                        </div>
                    </a>
                    `;
                }
            }
        }

        item_factory_attachments.innerHTML = attachment_html;
    } else {
        item_factory_attachments.innerHTML = '';
    }

    // TODO: scroller_apply_decoration breaks this atm, FIX!

    const processed = process_message_text(message_text);
    const final_text = processed.text;

    if (message.text.length > 0){
        item_factory_text.innerHTML += final_text;
    }

    let has_reactions = false;

    if ('reactions' in message) {
        const emoji_size = (touch_device ? 32 : 16);
        for (const reaction_id in message.reactions) {
            const reaction_count = message.reactions[reaction_id].length;
            if (reaction_count > 0) {
                has_reactions = true;
                const emoji = emoji_get_by_code(+reaction_id);
                if (emoji) {
                    const reaction_authors = message.reactions[reaction_id].map(id => ls_find_user(id) ? ls_find_user(id).name : '').join(', ');
                    const reaction_item = HTML(
                    `<div class="reaction" data-code="${+reaction_id}" title="${reaction_authors}">
                        <div class="emoji${emoji_size} e${emoji.s}"></div>
                        <span class="reaction-count">${reaction_count}</span>
                    </div>`);

                    item_factory_reactions.appendChild(reaction_item);
                }
            }
        }
    }
    
    if (message.reply_to >= 0) {
        if ('text' in messages[channel_id][message.reply_to]) {
            const author = ls_find_user(messages[channel_id][message.reply_to].author_id).name;
            const processed_original_no_newline = process_message_text(messages[channel_id][message.reply_to].text.replace(/\n/g, ' ')).text;
            item_factory_reply_text.innerHTML = processed_original_no_newline.length > 0 ? processed_original_no_newline : '[attachment]';
            item_factory_reply_author.innerHTML = author;
        } else {
            item_factory_reply_text.innerHTML = '';
            item_factory_reply_author.innerHTML = '';
        }
    } else {
        item_factory_reply_text.innerHTML = '';
    }

    if (author_index === me.user_id) {
        item_factor_actions.innerHTML = `
            <div class="action light more"><img draggable="false" src="static/icons/more.svg"></div>
            <div class="action light edit"><img draggable="false" src="static/icons/pencil.svg"></div>`;
    } else {
        item_factor_actions.innerHTML = `
            <div class="action light react"><img draggable="false" src="static/icons/react.svg"></div>
            <div class="action light reply"><img draggable="false" src="static/icons/reply.svg"></div>
            <div class="action light more"><img draggable="false" src="static/icons/more.svg"></div>`;
    }

    const result = item_factory_child.cloneNode(true);

    item_factory_reactions.innerHTML = '';

    if (index === item_factory_highlight) {
        result.classList.add('highlight');
    }

    if (author_index === me.user_id) {
        result.classList.add('mine');
    }
    if (!tmp) {
        let context_menu_actions = [
            {
                'text': 'React',
                'action': () => toggle_reaction_window(index, result.querySelector('.action.more'), author_index === me.user_id ? 'left' : 'right')
            },
            {
                'text': 'Reply',
                'action': () => handlers.reply(index, message)
            }
        ];

        if (!in_thread) {
            context_menu_actions.push({ 'text': 'Thread', 'action': () => thread_open(message.thread_id) },
                                      { 'text': 'Pin', 'action': () => enqueue_message_pin(channel_id, index) });
        }

        let more_icon = result.querySelector('.action.more');
        let edit_icon = result.querySelector('.action.edit');
        let reply_icon = result.querySelector('.action.reply');
        let react_icon = result.querySelector('.action.react');

        if (author_index === me.user_id) {
            make_clickable(edit_icon, () =>  handlers.edit(index, message));

            context_menu_actions.push({
                                          'text': 'Edit',
                                          'action': () => handlers.edit(index, message)
                                      },
                                      {
                                          'text': 'Delete',
                                          'action': () => show_delete_popup(index)
                                      });
        } else {
            make_clickable(reply_icon, () =>  handlers.reply(index, message));
            make_clickable(react_icon, (e) =>  toggle_reaction_window(index, more_icon));
        }

        make_clickable(more_icon, (e) => {
            const at = more_icon.getBoundingClientRect();
            context_menu_show(at, context_menu_actions);
        });
    }

    let thread_messages = messages[channel_id].filter(m => m.thread_id === message.thread_id && !m.deleted)
    if (!in_thread && message.type === RECORD.MESSAGE) {
        set_thread_info(result, message.thread_id, thread_messages.length);
    }
    if (touch_device) {
        const content = result.querySelector('.content');
        if (content) {
            make_button(content, (e) => {
                result.classList.add('highlight');
                setTimeout(() => {
                    result.classList.remove('highlight');
                }, 2000);

                // const at = result.querySelector('.content').getBoundingClientRect();
                const menu = divs['context-menu'].getBoundingClientRect();
                const message_actions = [];

                if (me.user_id === message.author_id) {
                    message_actions.push({
                                             'text': 'Edit',
                                             'action': () => {
                                                 handlers.edit(index, message);
                                                 result.classList.remove('longtouched');
                                             }
                                         });

                    message_actions.push({
                                             'text': 'Delete',
                                             'action': () => {
                                                 show_delete_popup(index)
                                                 result.classList.remove('longtouched');
                                             }
                                         });
                }

                message_actions.push({
                                         'text': 'React',
                                         'action': () => {
                                             toggle_reaction_window(index, result.querySelector('.action.more'))
                                             result.classList.remove('longtouched');
                                         }
                                     });

                message_actions.push({
                                         'text': 'Reply',
                                         'action': () => {
                                             handlers.reply(index, message);
                                             result.classList.remove('longtouched');
                                         }
                                     });
                if (!in_thread) {
                    message_actions.push({
                                             'text': 'Pin',
                                             'action': () => {
                                                 enqueue_message_pin(channel_id, index)
                                                 result.classList.remove('longtouched');
                                             }
                                         },
                                         {
                                             'text': 'Thread',
                                             'action': () => {
                                                 thread_open(message.thread_id);
                                                 result.classList.remove('longtouched');
                                             }
                                         });
                }
                context_menu_show_bottom_full(message_actions);
            });
        }
    }

    if ('reactions' in message) {
        const subst = (message.timestamp <= 1649864763);
        for (const reaction_id in message.reactions) {
            const reaction_count = message.reactions[reaction_id].length;
            if (reaction_count > 0) {
                const emoji = emoji_get_by_code(+reaction_id, subst);
                if (emoji) {
                    make_clickable(result.querySelector(`.reaction[data-code="${reaction_id}"]`), () => {
                        click_reaction(channel_id, index, +reaction_id);    
                    });
                }
            }
        }
    }

    if (has_reactions) {
        result.querySelector('.bot-row').classList.remove('dhide');
    }

    if (message.reply_to >= 0) {
        result.classList.add('reply');
        make_clickable(result.querySelector('.reply-preview'), () => {
            handlers.jump_to(message.reply_to);
        })
    }

    if (processed.emojied) {
        if (touch_device) {
            result.querySelectorAll('.text .emoji32').forEach((e) => {
                e.classList.remove('emoji32');
                e.classList.add('emoji64');
            });
            result.classList.add('emojied');
        } else {
            result.querySelectorAll('.text .emoji16').forEach((e) => {
                e.classList.remove('emoji16');
                e.classList.add('emoji32');
            });
            result.classList.add('emojied');
        }
    }

    if (tmp) {
        result.classList.add('tmp');
    }

    if ('attachments' in message) {
        for (const file of message['attachments']) {
            if (is_supported_image(file.ext)) {
                const img = result.querySelector(`img[data-file-id="f-${file.id}"]`);
                if (img) {
                    make_clickable(img, () => show_images_popup(file.id));
                    if (!img.classList.contains('big-img')) {
                        wait_for_image_preview(img, file.id, 1, 1000);
                    } else {
                        wait_for_image_preview(img, file.id, 0, 1000);
                    }
                }
            }
        }
    }

    let seen = false;
    const channel = ls_get_channel(channel_id);

    for (const user of channel.users) {
        if (user.id !== me.user_id && user.first_unseen > index) {
            seen = true;
            break;
        }
    }

    if (seen) {
        result.classList.add('seen');
    } else if (tmp) {
        result.classList.add('failed');
    } else {
        result.classList.add('acked');
    }

    if (!has_attachments) {
        result.querySelector('.attachments').remove();
    }

    if (images.length === 0) {
        result.querySelector('.images').remove();
    } else if (images.length === 1) {
        if (final_text.length === 0) {
            result.querySelector('.content').style['max-width'] = images[0].width + 'px';
        } else {
            result.querySelector('.content').style['max-width'] = Math.max(200, images[0].width) + 'px';
        }
    }

    if (message.text.length === 0) {
        result.querySelector('.text').classList.add('dhide');
    }

    if (!tmp) {
        result.setAttribute('data-index', index);
    }

    // result.classList.add('opac');
    // window.requestIdleCallback(() => {
    //  result.classList.remove('opac');
    // })

    return result;
}

function set_thread_info(element, thread_id, thread_length) {
    let thread_info = element.querySelector('.thread-row');
    if (thread_length > 1) {
        thread_info.classList.remove('dhide');
        thread_info.innerHTML = `<div>${thread_length} messages in thread</div>`;
        make_clickable(thread_info, () => {
            thread_open(thread_id);
        });
    } else {
        thread_info.classList.add('dhide');
    }
}

function create_channel_item(channel) {
    const unread_info = unread_message_info(channel.id);
    const title = get_channel_title(channel);
    const avatar = get_channel_avatar(channel, touch_device);
	let status_string = '';
    let user_id = '';

    let message_preview = '';
    let last_message_timestamp = '';

    if (touch_device) {
        const last_message = last_nondeleted_message(channel.id);
        if (last_message) {
            const message_date = new Date(last_message.timestamp * 1000);
            let hours = message_date.getHours();
            let minutes = message_date.getMinutes();

            if (hours < 10) hours = '0' + hours;
            if (minutes < 10) minutes = '0' + minutes;

            let last_message_content = process_message_text(last_message.text.split('\n')[0].substring(0, 128)).text;
            last_message_content = (last_message_content.length === 0 ? '[attachment]' : last_message_content);
            message_preview = `<span class="message-preview">${last_message_content}</span>`
            last_message_timestamp = `<span class="message-timestamp">${hours + ':' + minutes}</span>`;
        }
    }

    if (channel.is_dm) {
        const them = get_direct_partner(channel);
        if (them) {
            user_id = them.id;
            status_string = user_status_string(them);
        }
    }

    const result = HTML(`<div class="channel ${channel.is_dm ? 'dm': ''}" data-channel-id="${channel.id}">
        <div class="avatar"><img draggable="false" src="${avatar}"></div>
        ${circle_status_indicator(status_string, user_id)}
        <div class="title">
            ${title}
            ${message_preview}
            ${last_message_timestamp}
        </div>
        <div class="right">
            <div class="unread${unread_info.has_mentions ? ' ping-me' : ''}">${unread_info.unread_string}</div>
        </div>
    </div>`);

    make_clickable(result, () => { 
        divs['sidebar'].classList.remove('shown');
        
        // Asking for notitication permission is only allowed as a reaction
        // to a user gesture, so we put it here, in channel 'click' handler
        if ('Notification' in window) {
            if (window.Notification.permission === 'default') {
                window.Notification.requestPermission();
            }
        }

        switch_to_channel(channel.id); 
    });

    return result;

    // <img class="pin-icon" src="static/icons/pin_aaa.svg" style="height: 14px;">
}

function add_typing_user_for_channel(typing_users) {
    if (typing_users.length > 3) {
        return 'Multiple people are typing...';
    }

    if (typing_users.length === 0) {
        return '';
    }

    const logins = typing_users.map(i => i.user_login).join(', ');
    return logins + (typing_users.length === 1 ? ' is' : ' are')  + ' typing...';
}

function user_autocomplete_variant(user) {
    return HTML(`<div class="variant">
        <span>${user.name}</span> <span style="color: var(--de-emph)">@${user.login}</span>
    </div>`);
}

function invite_panel_html(channel_id) {
    const channel = ls_get_channel(channel_id);
    if (!channel) return `<span>Channel ${channel_id} not found</span>`;

    let result = '';
    const now_unix = Math.floor(new Date().getTime() / 1000);

    result += '<div class="channel-members-list clickable">'
    const users = ls_get_users();
    for (const user of users) {
        if (channel.users.map(u => u.id).includes(user.id)) {
            continue;
        }

        const last_seen = last_seen_string(user, now_unix);
        const status_string = user_status_string(user);
        const role = (user.id === 0 ? 'admin' : '');

        result += 
        `<div class="channel-member" data-user-id="${user.id}" onclick="toggle_invite_selected(${user.id})">
            <div class="flex-center-gap">
                <div class="avatar"><img draggable="false" src="static/images/ph.svg"></div>
                <span>${user.name}</span>
                ${text_status_indicator(last_seen, user.id)}
            </div>
            <div class="flex-center-gap">
                <span class="member-role">
                    ${role}
                </span>
                <div class="checkbox"><div class="heart"></div></div>
            </div>
        </div>`;
    }

    result += '</div>'

    return result;
}

function last_seen_string(user, now_unix) {
    if (user.status === USER_STATUS.ONLINE) {
        return 'online';
    }

    if (!('last_online' in user)) {
        return 'offline';
    }

    const time_diff = now_unix - user.last_online;
    let diff_string = null;

    if (time_diff < 60) {
        diff_string = 'recently';
    } else if (time_diff < 60 * 60) {
        const minutes = Math.floor(time_diff / 60);
        diff_string = `${minutes} ${maybe_add_s('minute', minutes)} ago`;
    } else if (time_diff < 60 * 60 * 24) {
        const hours = Math.floor(time_diff / (60 * 60));
        diff_string = `${hours} ${maybe_add_s('hour', hours)} ago`;
    } else {
        const days = Math.floor(time_diff / (60 * 60 * 24));
        if (days < 100) {
            diff_string = `${days} ${maybe_add_s('day', days)} ago`;
        } else {
            diff_string = `many days ago`;
        }
    }

    return diff_string;
}

function channel_info_html(channel_id) {
    const channel = ls_get_channel(channel_id);
    if (!channel) return `<span>Channel ${channel_id} not found</span>`;

    let result = '';
    let online_users = 0;
    const now_unix = Math.floor(new Date().getTime() / 1000);

    for (const u of channel.users) {
        const user = ls_find_user(u.id);
        if (user && user.status === USER_STATUS.ONLINE) {
            ++online_users;
        }
    }


    result += `<div class="popup-channel-title">
                    <div class="avatar">
                        <img draggable="false" src="static/images/ph.svg">
                    </div>
                    <span>${channel.title}</span>
                </div>`;
    result += 
    `<div class="popup-member-list-header">
        ${channel.users.length} members, ${online_users} online
    </div>`;

    // result += '<button id="invite-members" class="secondary">Invite</button>'
    result += '</div>'

    result += '<div class="channel-members-list">'

    const channel_users = channel.users.toSorted((a, b) => {
        // TODO: @speed :(((((
        const u1 = ls_find_user(a.id);
        const u2 = ls_find_user(b.id);

        if (u1.status === USER_STATUS.ONLINE && u2.status !== u1.status) {
            return -1;
        }

        if (u2.status === USER_STATUS.ONLINE && u1.status !== u2.status) {
            return 1;
        }

        return u2.last_online - u1.last_online;
    })

    for (const u of channel_users) {
        const user = ls_find_user(u.id); // TODO: @speed N^2

        const last_seen = last_seen_string(user, now_unix);
        const role = (u.id === 0 ? 'admin' : '');
        const status_string = user_status_string(user);

        result += 
        `<div class="channel-member" data-user-id="${u.id}">
            <div class="flex-center-gap">
                <div class="avatar"><img draggable="false" src="static/images/ph.svg"></div>
                ${circle_status_indicator(status_string, user.id)}
                <span>${user.name}</span>
                ${text_status_indicator(last_seen, user.id)}
            </div>
            <div class="flex-center-gap">
                <span class="member-role">
                    ${role}
                </span>
            </div>
        </div>`;
    }

    result += '</div>'

    return result;
}

function channel_change_name_html(channel) {
    return `
        <div>
        <label>Channel name:</label>
        <input id="change-channel-title-input" type="text" class="text-input" maxlength="30" value="${channel.title}">
        </div>`
}

function maybe_add_s(word, number) {
    if (number === 1) {
        return word;
    } else {
        return word + 's';
    }
}

function show_images_popup(around) {
    const channel_attachments = get_attachments(ls_get_current_channel_id());
    
    let index = -1;
    for (let i = 0; i < channel_attachments.length; ++i) {
        const attachment = channel_attachments[i];
        if (attachment.id === around) {
            index = i;
            break;
        }
    }

    if (index === -1) {
        return;
    }

    const text = 
    `<div class="img-viewer" tabindex="0">
        <img draggable="false" src="/storage/${around}-0">
        <div class="img-viewer-header">
            <div class="text">${index + 1} of ${channel_attachments.length}</div>
            <div class="links"><a target="_blank" href="/storage/${around}">Original</a></div>
        </div>
    </div>`;

    popup_show('', text, [], true);

    const viewer = document.querySelector('.img-viewer');
    const img = document.querySelector('.img-viewer img');
    const header_text = document.querySelector('.img-viewer-header .text');
    const header_link = document.querySelector('.img-viewer-header .links a');

    viewer.focus();

    viewer.addEventListener('keydown', (e) => {
        if (e.code === 'ArrowLeft') {
            index--;
            if (index < 0) {
                index = channel_attachments.length - 1;
            }
            img.src = '/storage/' + channel_attachments[index].id + '-0';
            header_text.innerHTML = `${index + 1} of ${channel_attachments.length}`;
            header_link.href = `/storage/${channel_attachments[index].id}`;
        } else if (e.code === 'ArrowRight') {
            index++;
            if (index == channel_attachments.length) {
                index = 0;
            }
            img.src = '/storage/' + channel_attachments[index].id + '-0';
            header_text.innerHTML = `${index + 1} of ${channel_attachments.length}`;
            header_link.href = `/storage/${channel_attachments[index].id}`;
        }
    })
}

function channel_attachments_html(channel_id) {
    if (!(channel_id in messages)) {
        return `<div>Channel ${channel_id} not found!</div>`;
    }

    let result = '<div class="all-attachments">';

    for (const record of messages[channel_id]) {
        if (record.type === RECORD.MESSAGE && !record.deleted) {
            if ('attachments' in record) {
                for (const attachment of record['attachments']) {
                    if (is_supported_image(attachment.ext)) {
                        result += `<div onclick="show_images_popup('${attachment.id}')" class="all-attachments-attachment-preview" style="background: var(--de-de-emph) url('/storage/${attachment.id}-2')"></div>`;
                    }
                }
            }
        }
    }

    result += '</div>';

    return result;
}

function get_attachment_preview_item(file, file_id_hex, remove_callback) {
    let item = null;
    if (is_supported_image(uploader_server_ext(file.type))) {

        const url = URL.createObjectURL(file);
        // .any-attachment and .preloader-wrapper for querying both image and file attachments in progress updated
        item = HTML(`
        <div class="attachment any-attachment" data-file-id="f-${file_id_hex}">
            <div class="attachment-remove" title="Remove attachment"><img draggable="false" src="/static/icons/cancel_333.svg"></div>
            <div class="img-preloader-wrapper preloader-wrapper">
                <div class="img-preloader-full">
                    <div class="img-preloader-progress"></div>
                </div>
            </div>
            <img draggable="false" class="main-image" src="${url}" draggable="false">
        </div>`);

    } else {
        item = HTML(`
        <div class="file-attachment any-attachment" data-file-id="f-${file_id_hex}">
            <div class="file-attachment-preloader preloader-wrapper">
                <div class="img-preloader-full">
                    <div class="img-preloader-progress"></div>
                </div>
            </div>
            <img draggable="false" class="upload-done dhide" src="static/icons/tick.svg">
            <div class="file-attachment-info">
                ${file.name}
                <div class="attachment-remove" title="Remove attachment"><img draggable="false" src="/static/icons/cancel_333.svg"></div>
            </div>
        </div>
        `);
    }

    make_clickable(item.querySelector('.attachment-remove'), (e) => remove_callback(file_id_hex));
    return item;
}

function text_status_indicator(status, user_id) {
    return `<span class ="text-status-indicator" data-user-id="${user_id}">${status}</span>`
}

function circle_status_indicator(status, user_id) {
    return `<div class="circle-status-indicator indicator ${status}" data-user-id="${user_id}"></div>`
}
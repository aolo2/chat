const firefox = (navigator.userAgent.indexOf('Firefox') !== -1);
const chromium = !!window.chrome;
const touch_device = (('ontouchstart' in window) || (navigator.maxTouchPoints > 0) || (navigator.msMaxTouchPoints > 0));

let debouncer = {};
let channel_scroller = null;
let channel_list_spinner = null;

function debounce(call, timeout) {
	// TODO
}

function visible_message_info(msgs) {
	let visible_messages =  msgs.filter(m => m.type === RECORD.MESSAGE && m.deleted !== true);
	let was_mentioned = false;
	for (let message of visible_messages) {
		if (has_mentions(message.text)) {
			was_mentioned = true;
			break;
		}
	}
	return { 'count': visible_messages.length, 'has_mentions': was_mentioned} ;
}

function unread_message_info(channel_id) {
	const channel = ls_get_channel(channel_id);
	let real_first_unseen = channel.client.first_unseen;

	for (let i = real_first_unseen; i < messages[channel.id].length; ++i) {
		real_first_unseen = i;
		if (messages[channel.id][i].type === RECORD.MESSAGE) {
			break;
		}
	}

	// TODO @speed: this might copy a lot of data if the user has a lot of unread messages
	const unseen_from_first_actual_message = messages[channel.id].slice(real_first_unseen);
	const unread_info = visible_message_info(unseen_from_first_actual_message);
	let unread_string = unread_info.count > 0 ? unread_info.count : '';
	return { unread_string, 'has_mentions': unread_info.has_mentions };
}

function apply_decoration(channel, record) {
	// TODO: why??
	if (record.message_id === -1) {
		return;
	}

	const channel_id = channel.id;

	switch (record.type) {
	    case RECORD.DELETE: {
	        messages[channel_id][record.message_id].deleted = true;
	        display_local = true; // TODO: use

	        // Nuke pinned message if it was the one that got deleted
	        // Here because message_id might point outside of active scroller range
            const maybe_pinned = document.querySelector(`#pin-preview-text[data-message-id="${record.message_id}"]`);
            if (maybe_pinned) {
                pin_message(channel_id, -1);
            }

	        break;
	    }

	    case RECORD.EDIT: {
	        messages[channel_id][record.message_id].text = record.text;
	        display_local = true;

	        // Redraw pinned message if it was the one being edited
	        // Here because message_id might point outside of active scroller range
            const maybe_pinned = document.querySelector(`#pin-preview-text[data-message-id="${record.message_id}"]`);
            if (maybe_pinned) {
                restore_pin(channel_id);
            }

	        break;
	    }

	    case RECORD.REPLY: {
	        messages[channel_id][record.message_id].reply_to = record.reply_to;
	        display_local = true;
	        break;
	    }

	    case RECORD.REACTION_ADD: {
	    	if (record.message_id >= messages[channel_id].length) {
	    		console.error(`Message references a non-existant message ${record.message_id}`);
	    		break;
	    	}

	        const referenced_message = messages[channel_id][record.message_id];

	        if (!('reactions' in referenced_message)) {
	            referenced_message.reactions = {};
	        }

	        if (!(record.reaction_id in referenced_message.reactions)) {
	            referenced_message.reactions[record.reaction_id] = [];
	        }

	        if (referenced_message.reactions[record.reaction_id].indexOf(record.author_id) === -1) {
	            referenced_message.reactions[record.reaction_id].push(record.author_id);
	        }

	        break;
	    }

	    case RECORD.REACTION_REMOVE: {
	    	if (record.message_id >= messages[channel_id].length) {
	    		console.error(`Message references a non-existant message ${record.message_id}`);
	    		break;
	    	}

	        const referenced_message = messages[channel_id][record.message_id];

	        if (!(record.reaction_id in referenced_message.reactions)) {
	        	// NOTE(aolo2): should not happen (deletion of non-existent reaction) but apparently does
	        	break;
	        }

	        const reaction_at = referenced_message.reactions[record.reaction_id].indexOf(record.author_id);

	        if (reaction_at !== -1) {
	        	referenced_message.reactions[record.reaction_id].splice(reaction_at, 1); 
	        }

	        break;
	    }

	    case RECORD.PIN: {
	    	// NOTE(aolo2): we do this here because scroller only works with good record.message_id
	    	// This might have message_id outside of scroller range
	    	const pinned_message = record.message_id;
            pin_message(channel_id, pinned_message);
	    	break;
	    }

	    case RECORD.ATTACH: {
	    	const referenced_message = messages[channel_id][record.message_id];
	    	if (!('attachments' in referenced_message)) {
	    		referenced_message.attachments = [];
	    	}

	    	const attach = {
	    		'id': record.id,
	    		'ext': record.ext,
	    	};

	    	if ('width' in record && 'height' in record) {
	    		attach.width = record.width;
	    		attach.height = record.height;
	    	} else if ('name' in record) {
	    		attach.name = record.name;
	    	} else {
	    		console.error(`Attach doesn't have name nor dimensions!`);
	    		attach.name = record.id;
	    	}

	    	referenced_message.attachments.push(attach);

	    	break;
	    }

		case RECORD._LEGACY_USER_LEFT:
		case RECORD._LEGACY_USER_JOINED:
		case RECORD.MESSAGE: {
			// No special hanlding needed, but we still want the default case
			// so that we don't forget to add any new message types
			break;
		}

        case RECORD.USER_JOINED: {
            if ('users' in channel) {
                channel.users.push({ 'first_unseen': 0, 'id': record.user_id });
            }
            break;
        }

        case RECORD.USER_LEFT: {
            channel.users = channel.users.filter(i => i.id !== record.user_id);
            break;
        }

		case RECORD.TITLE_CHANGED: {
            channel.title = record.title;
            divs['selected-channel-title'].innerHTML = record.title;
            break;
        }

		default: {
			console.log('WARNING: unhandled decoration type', record.type);
		}
	}

	channel_scroller.apply_decoration(channel_id, record);
	thread_content_block.scroller.apply_decoration(channel_id, record);
}

function count_attachments(channel_id) {
	let count = 0;

	if (!(channel_id in messages)) {
		return 0;
	}

	for (const record of messages[channel_id]) {
	    if (record.type === RECORD.MESSAGE && !record.deleted) {
	        if ('attachments' in record) {
	            for (const file_id_hex of record['attachments']) {
	                count++;
	            }
	        }
	    }
	}

	return count;
}

function get_attachments(channel_id) {
	const result = [];

	if (!(channel_id in messages)) {
		return result;
	}

	for (const record of messages[channel_id]) {
	    if (record.type === RECORD.MESSAGE && !record.deleted) {
	        if ('attachments' in record) {
	            for (const attachment of record['attachments']) {
	            	if (is_supported_image(attachment.ext)) {
	                	result.push(attachment);
	                }
	            }
	        }
	    }
	}

	return result;
}

function click_reaction(channel_id, message_id, code) {
	const message = messages[channel_id][message_id];
	const me = ls_get_me();
	if (message.reactions[code].indexOf(me.user_id) === -1) {
		enqueue_reaction_add(ls_get_current_channel_id(), message_id, code);
	} else {
		enqueue_reaction_remove(ls_get_current_channel_id(), message_id, code);
	}
}

function initiate_edit(index) {
	const message = messages[ls_get_current_channel_id()][index];
	editing_index = index;
	set_message_input(divs['message-input'], message.text);
	divs['reply-preview-author'].innerHTML = ls_find_user(message.author_id).name;
	divs['reply-preview-text'].innerHTML = process_message_text(message.text).text;
	divs['message-input-additions'].classList.remove('dhide');
	if (!touch_device) divs['message-input'].focus();
	l_message_input(divs['message-input']);
}

function initiate_reply(index, message) {
	replying_index = index;
	divs['reply-preview-author'].innerHTML = ls_find_user(message.author_id).name;
	divs['reply-preview-text'].innerHTML = process_message_text(message.text).text;
	divs['message-input-additions'].classList.remove('dhide');
	if (!touch_device) divs['message-input'].focus();
	l_message_input(divs['message-input']);
}

function pin_message(channel_id, index) {
	if (channel_id !== ls_get_current_channel_id()) {
		return;
	}

	if (index !== 0xFFFFFFFF && index !== -1) {
		// Pin
		pinned_index = index;
		const message = messages[channel_id][index];
		// NOTE(aolo2): this fails only in case of a corrupted message pin
		if (message.type === RECORD.MESSAGE) {
			const user = ls_find_user(message.author_id);
			if (user) find('pin-preview-author').innerHTML = user.name; // TODO: delay this until users are loaded (on first load)
			find('pin-preview-text').innerHTML = process_message_text(message.text).text;
			find('pin-preview-container').classList.remove('dhide');
			find('pin-preview-text').setAttribute('data-message-id', index);
		}
	} else {
		// Unpin
		pinned_index = -1;
		find('pin-preview-container').classList.add('dhide');
	}
}

function refresh_message_send() {
	if (active_uploads > 0) {
		divs['message-send-button'].classList.add('tdisabled');
	} else {
		divs['message-send-button'].classList.remove('tdisabled');
	}
}

async function logout() {
	localStorage.clear();
	sessionStorage.clear();
	await idb_clear();
	window.location.href = '/login.html';
}

function get_last_mine_message() {
	const me = ls_get_me();
	for (let i = messages[ls_get_current_channel_id()].length - 1; i >= 0; --i) {
		const message = messages[ls_get_current_channel_id()][i];
		if (message.type === RECORD.MESSAGE && message.author_id === me.user_id && !message.deleted) {
			return i;
		}
	}

	return -1;
}

function cancel_edit() {
	editing_index = null;
	clear_message_input(divs['message-input']);
	divs['message-input-additions'].classList.add('dhide');
}

function cancel_reply() {
	replying_index = null;
	clear_message_input(divs['message-input']);
	divs['message-input-additions'].classList.add('dhide');
}

function cancel_attach() {
	attaching_files = [];
	cancelled_files = [];
	divs['msg-attachments-row'].innerHTML = '';
	divs['msg-attachments-files-row'].innerHTML = '';

	divs['msg-attachments-row'].classList.add('dhide');
	divs['msg-attachments-files-row'].classList.add('dhide');
}

function init_storage_sync() {
	setInterval(() => {
		for (const channel_id in messages) {
        	idb_save(channel_id, messages[channel_id]);
		}
    }, CONFIG_STORAGE_SAVE_INTERVAL);
}

function restore_pin(channel_id) {
	if (channel_id === null) return;
	if (!(channel_id in messages)) return;

	// @speed do not traverse all messages, just save the current pinned one
	let deleted_records = [];
	for (let i = messages[channel_id].length - 1; i >= 0; --i) {
		const record = messages[channel_id][i];
		if (record.type === RECORD.DELETE) {
			deleted_records.push(record.message_id);
		} else if (record.type === RECORD.PIN) {
			if (!deleted_records.includes(record.message_id)) {
				pin_message(channel_id, record.message_id);
			}
			return;
		}
	}

	// Nothing is pinned in this channel 
	pin_message(channel_id, -1);
}

function last_nondeleted_message_index(channel_id) {
	if (channel_id in messages) {
		const channel_messages = messages[channel_id];
		
		for (let i = channel_messages.length - 1; i >= 0; --i) {
			const record = channel_messages[i];
			if (record.type === RECORD.MESSAGE && !record.deleted) {
				return i;
			}
		}
	}

	return null;
}

function last_nondeleted_message(channel_id) {
	const index = last_nondeleted_message_index(channel_id);

	if (index !== null) {
		return messages[channel_id][index];
	}

	return null;
}

function remove_channel(channel_id) {
	ls_leave_channel(parseInt(channel_id));
	const old_item = document.querySelector(`.channel[data-channel-id="${channel_id}"]`);
	if (old_item) {
		const next = old_item.nextSibling;
		old_item.remove();
		return next;
	}
}

function redraw_channel(channel) {
	const old_item = document.querySelector(`.channel[data-channel-id="${channel.id}"]`);
	const new_item = create_channel_item(channel);

	if (old_item) {
		old_item.remove();
	}

	const our_latest_record = last_nondeleted_message(channel.id);
	
	if (our_latest_record) {
		// Channels should be sorted by their latest non-deleted message date, at least somewhat :)
		// @speed This is N^2, because we are receiving channel-info one by one for now
		for (const channel_element of divs['channel-list'].children) {
			const channel_id = channel_element.getAttribute('data-channel-id');
			const record = last_nondeleted_message(channel_id);
			if (record && record.timestamp < our_latest_record.timestamp) {
				divs['channel-list'].insertBefore(new_item, channel_element);
				return;
			}
		}
	}

	divs['channel-list'].appendChild(new_item);
}

let filter_thread_cache = null;
function set_default_filter(on_complete) {
	filter_thread_cache = {};
	channel_scroller.set_filter(channel_scroller_filter, on_complete);
}

function switch_to_channel(id) {
	const old_id = ls_get_current_channel_id();
	const now_unix = Math.floor(new Date().getTime() / 1000);

	if (id === null) {
		document.querySelector('.channel-content').classList.add('dhide');
		document.querySelector('.settings').classList.add('dhide');
		document.querySelector('.channel-creation').classList.remove('dhide');
	} else {
		document.querySelector('.settings').classList.add('dhide');
		document.querySelector('.channel-creation').classList.add('dhide');
		document.querySelector('.channel-content').classList.remove('dhide');
	}

	if (!(id in messages)) {
		messages[id] = [];
	}

	// TODO
	// window.history.pushState(`channel-${id}`, '', `/channel-${id}`);

	document.querySelectorAll('.channel.selected').forEach(i => i.classList.remove('selected'));

	if (id !== null) {
		const channel_item = document.querySelector(`.channel[data-channel-id="${id}"]`);
		if (channel_item) channel_item.classList.add('selected');
	}

	ls_set_current_channel(id);
	channel_scroller.set_channel(id);
	set_default_filter();
	thread_close();

    const channel = ls_get_channel(id);
    if (channel !== null) {
    	let channel_title = get_channel_title(channel);
    	
    	if (channel.is_dm) {
    		divs['toggle-people'].classList.add('dhide');
    		const other = get_direct_partner(channel);
    		const last_seen = last_seen_string(other, now_unix);

    		channel_title += text_status_indicator(last_seen, other.id);
    	} else {
    		divs['toggle-people'].classList.remove('dhide');
    	}

    	divs['selected-channel-title'].innerHTML = channel_title;
    	document.querySelector('#selected-channel-avatar img').src = get_channel_avatar(channel, true);

    	restore_pin(id);
    }

    if (id !== old_id) {
    	search_disable();
    	hide_reaction_window();
    	cancel_edit();
    	cancel_reply();
		refresh_typing_users_list();

    	if (!touch_device) {
    		divs['message-input'].focus();
    	} else if (id !== null) {
    		divs['container'].focus();
    	}
    }

    if (id !== null) {
    	divs['sidebar'].classList.remove('shown');
    }
}

function get_message_autocomplete(query) {
	if (query.length === 0 || query[0] !== '@') {
		return [];
	}

	const query_uppercase = query.slice(1).toUpperCase();

	const current_channel_id = ls_get_current_channel_id();
	const current_channel = ls_get_channel(current_channel_id);

	if (!current_channel) {
		return [];
	}

	const result = [];

	for (const user_info of current_channel.users) {
		const user = ls_find_user(user_info.id);
		if (user !== null) {
			if (query.length === 0 || user.name.toUpperCase().includes(query_uppercase) || user.login.toUpperCase().includes(query_uppercase)) {
				result.push({
					'content_left': user.name,
					'content_right': user.login,
					'text': '@' + user.login
				});
			}
		}
	}

	return result;
}

function move_channel_to_top(channel_id) {
	const channel_item = document.querySelector(`.channel[data-channel-id="${channel_id}"]`);
	if (channel_item && channel_item !== divs['channel-list'].firstChild) {
		divs['channel-list'].insertBefore(channel_item, divs['channel-list'].firstChild);
	}
}
function update_status(user, element = divs['main-page']) {
	const now_unix = Math.floor(new Date().getTime() / 1000);
	let text_indicators = element.querySelectorAll(`.text-status-indicator[data-user-id="${user.id}"]`);
	text_indicators.forEach(indicator => {
		indicator.innerHTML = last_seen_string(user, now_unix);
	})
	let circle_indicators = element.querySelectorAll(`.circle-status-indicator[data-user-id="${user.id}"]`);
	circle_indicators.forEach(indicator => {
		const status_string = user_status_string(user);
		indicator.classList.remove("offline", "online", "away", "busy");
		indicator.classList.add(status_string);
	})
}

function toggle_invite_selected(user_id) {
	const item = document.querySelector(`.channel-member[data-user-id="${user_id}"]`);
	if (item) {
		item.classList.toggle('selected');
		if (item.classList.contains('selected')) {
			invite_selected.push(user_id);
		} else {
			const index = invite_selected.indexOf(user_id);
			if (index >= 0) {
				invite_selected.splice(index, 1);	
			}
		}
	}

	if (invite_selected.length > 0) {
		document.querySelector('button.invite-confirm').classList.remove('tdisabled');
	} else {
		document.querySelector('button.invite-confirm').classList.add('tdisabled');
	}
}

async function init_fast_path(me) {
	const channel_list = ls_get_channel_list();
	const current_channel = ls_get_current_channel_id();

	// Restore messages from indexedDB
    const results = await Promise.all(channel_list.map(id => idb_restore(id)));
    for (let i = 0; i < channel_list.length; ++i) {
    	const channel_id = channel_list[i];
    	const channel_messages = results[i] || [];
    	messages[channel_id] = channel_messages;
    }

    switch_to_channel(current_channel);

    // NOTE(aolo2): send_init MUST be called AFTER restoring messages and switching to channel,
    // so that message arrays are already in place by the time we send the init message
    websocket_connect(CONFIG_WS_URL, () => {
    	// TODO @speed: come up with a fast path for init
		websocket_send_init(me.session_id, channel_list);
    });
}

function init_slow_path(me) {
	switch_to_channel(null);
    websocket_connect(CONFIG_WS_URL, () => {
		websocket_send_init(me.session_id, []);
    });
}

function check_logged_in() {
	const me = ls_get_me();
    if (!me || !me.session_id) {
		l_logout();
    }
}

function scroller_init() {
	channel_scroller = new Scroller({root: find('container'), handle_reply: initiate_reply, handle_edit: initiate_edit, in_thread: false});
	channel_scroller.init();
}

async function main() {
    find_and_bind();
    emoji_init();
    popup_init();
    context_menu_init();
    websocket_init();
    scroller_init();
	thread_init(find('thread-content'));
    notification_init();

    ls_init();        // local storage

    const darkmode = ls_get_darkmode();

    if (darkmode === true) {
		divs['dark-mode-toggle'].checked = true;
        divs['main-page'].classList.add('dark');
    }

    const me = ls_get_me();
    if (!me || !me.session_id) {
    	l_logout(); // noreturn    	
    }

    try {
    	const idb_result = await idb_init(); // indexed db
    } catch (idb_ex) {
    	console.error(idb_ex);
    	return;
    }

    check_logged_in();

    // Have we just hit F5 or opened the page for the first time?
    if (!ls_get_first_load()) {
    	await init_fast_path(me);
    } else {
    	init_slow_path(me);
    	ls_set_first_load(false);
    }

    init_storage_sync();
	channel_list_spinner = spinner_init(document.querySelector('#channel-list-spinner'));
    channel_list_spinner.start();
}

document.addEventListener('DOMContentLoaded', main, false);

const RECORD_FIXED_SIZE = 1 + 2 + 4 + (4 + 8 + 4 + 4);

function serializer_create(size) {
    const buffer = new ArrayBuffer(size);
    return {
        'offset': 0,
        'size': size,
        'buffer': buffer,
        'view': new DataView(buffer),
        'strview': new Uint8Array(buffer),
    };
}

function serializer_u8(s, value) {
    s.view.setUint8(s.offset, value);
    s.offset += 1;
}

function serializer_u16(s, value) {
    s.view.setUint16(s.offset, value, true);
    s.offset += 2;
}

function serializer_u32(s, value) {
    s.view.setUint32(s.offset, value, true);
    s.offset += 4;
}

function serializer_u64(s, value) {
    s.view.setBigUint64(s.offset, value, true);
    s.offset += 8;
}

function serializer_bytes(s, value) {
    s.strview.set(value, s.offset);
    s.offset += value.byteLength;
}

function serializer_text(s, value) {
    const bytes = utf8encoder.encode(value);
    s.strview.set(bytes, s.offset);
    s.offset += bytes.byteLength;
}

function serializer_advance(s, by) {
    if (s.offset + by > s.size) {
        console.error('Value passed to serializer_advance is too large', by);
        return;
    }

    s.offset += by;
}

function serializer_set(s, offset) {
    if (offset > s.size) {
        console.error('Offset passed to serializer_set is too large', offset);
        return;
    }

    s.offset = offset;
}

function serialize_record(s, record) {
    const starting_offset = s.offset;

    if (!('size' in record)) {
        console.error('No size in record');
        return false;
    }

    if (s.offset + record.size > s.size) {
        console.error('Serializer overflow');
        return false;
    }

    serializer_u8(s, record.type);
    serializer_u16(s, record.size);
    serializer_u32(s, record.message_id || 0);

    switch (record.type) {
        case RECORD.MESSAGE: {
            const bytes = utf8encoder.encode(record.text);
            serializer_u32(s, record.author_id);
            serializer_advance(s, 8); // place for timestamp
            serializer_u32(s, record.thread_id);
            serializer_u32(s, bytes.byteLength);
            serializer_set(s, starting_offset + RECORD_FIXED_SIZE);
            serializer_bytes(s, bytes);
            break;
        }

        case RECORD.PIN:
        case RECORD.DELETE: {
            break;
        }

        case RECORD.EDIT: {
            const bytes = utf8encoder.encode(record.text);
            serializer_u32(s, bytes.byteLength);
            serializer_set(s, starting_offset + RECORD_FIXED_SIZE);
            serializer_bytes(s, bytes);
            break;
        }

        case RECORD.REPLY: {
            serializer_u32(s, record.reply_to);
            break;
        }

        case RECORD.ATTACH: {
            const file_id = BigInt('0x' + record.file_id);
            const file_ext = record.file_ext;
            
            serializer_u64(s, file_id);
            serializer_u32(s, file_ext);

            if ('width' in record && 'height' in record) {
                serializer_u16(s, record.width);
                serializer_u16(s, record.height);
            } else {
                const bytes = utf8encoder.encode(record.file_name);
                serializer_u16(s, bytes.byteLength);
                serializer_set(s, starting_offset + RECORD_FIXED_SIZE);
                serializer_bytes(s, bytes);
            }

            break;
        }

        case RECORD.REACTION_ADD:
        case RECORD.REACTION_REMOVE: {
            serializer_u32(s, record.reaction_id);
            serializer_u32(s, record.author_id);
            break;
        }

        case RECORD.TITLE_CHANGED: {
            const bytes = utf8encoder.encode(record.text);

            serializer_u32(s, record.user_id);
            serializer_advance(s, 8); // place for timestamp
            serializer_u32(s, record.length);
            serializer_set(s, starting_offset + RECORD_FIXED_SIZE);
            serializer_bytes(s, bytes);
            break;
        }
        case RECORD.USER_LEFT:
        case RECORD.USER_JOINED: {
            serializer_u32(s, record.user_id);
            serializer_advance(s, 8); // place for timestamp
            break
        }

        default: {
            console.error('Unhandled record type in serialize_record', record.type);
            s.offset = starting_offset;
            return false;
        }
    }

    serializer_set(s, starting_offset + record.size);

    return true;
}

function random_int(min, max) { // [min,max)
    min = Math.ceil(min);
    max = Math.floor(max);
    return Math.floor(Math.random() * (max - min) + min);
}

function push_unsent(channel_id) {
    const channel = ls_get_channel(channel_id);
    if (!channel) {
        console.error('Attempt to push_unsent for a non-existent channel', channel_id);
        return;
    }

    if (channel.server.client_sn === channel.client.client_sn) {
        return;
    }

    const record_bytes = channel.client.queue.map(r => r.size).reduce((a, b) => a + b); // NOTE(aolo2): reduce a.size + b.size not working for some reason?
    const nmessages = channel.client.client_sn - channel.server.client_sn;
    const s = serializer_create(1 + 4 + 4 + 4 + record_bytes);

    serializer_u8(s, WS_MESSAGE_TYPE.CLIENT.SYNC);
    serializer_u32(s, channel.id);
    serializer_u32(s, channel.client.client_sn);
    serializer_u32(s, nmessages);

	if (CONFIG_DEBUG_PRINT) {
		console.debug("Sending SYN for channel", channel.id, "sn =", channel.client.client_sn, "nmessages =", nmessages);
	}

    for (const record of channel.client.queue) {
        serialize_record(s, record);
    }

    // NOTE(aolo2): on some firefoxes this can throw when we are offline
    try {
        if (ws) ws.send(s.buffer);
    } catch (e) {}

    const key = channel_id + '-' + channel.client.client_sn;
    channel.client.timeouts[key] = setTimeout(() => push_unsent(channel_id), CONFIG_WS_RESEND_INTERVAL);
}

function _enqueue(channel_id, record, skip_push = false) {
    const channel = ls_get_channel(channel_id);
    if (!channel) {
        console.error('Attempt to send message to a non-existent channel', channel_id);
        return;
    }

    if (record.type === RECORD.MESSAGE) {
        // const key = channel.client.client_sn;
        // scroller_follow_mode();
        // record.placeholder_timer = setTimeout(() => {
        //     scroller_add_tmp(record, key);
        // }, CONFIG_MESSAGE_PLACEHOLDER_DELAY);
    } else {
        // apply_decoration(channel_id, record);
        // TODO: this is VERY important to enable, so that the UI feels fast
    }

    channel.client.client_sn++;
    channel.client.queue.push(record);

    if (ls_set_channel(channel)) {
        // NOTE(aolo2): currently we send decorations as a record right after
        // the message with decoration.message_id = -1. For this to work we
        // need to send the message and the decoration in one batch, hence the skip
        if (!skip_push) {
            push_unsent(channel_id);
        }
    }
}

function enqueue_message(channel_id, thread_id, text, skip_push = false) {
    // NOTE(aolo2): Uint8Array can't be parsed back after doing a JSON.stringify, so convert
    // to bytes to know the length, but save the text to the queue
    // @typedarray-json
    const bytes = utf8encoder.encode(text);
    const me = ls_get_me();

    const record = { 
        'type': RECORD.MESSAGE, 
        'size': RECORD_FIXED_SIZE + bytes.byteLength,
        'author_id': me.user_id,
        'thread_id': thread_id ? thread_id : random_int(1024, 1024 * 1024 * 1024),
        'text': text,
        'timestamp': Math.floor(Date.now() / 1000),
    };

    _enqueue(channel_id, record, skip_push);
}

function enqueue_delete(channel_id, message_id) {
    const record = {
        'type': RECORD.DELETE,
        'size': RECORD_FIXED_SIZE,
        'message_id': message_id,
    };
    _enqueue(channel_id, record);
}

function enqueue_edit(channel_id, message_id, text, skip_push = false) {
    // NOTE(aolo2): see @typedarray-json

    const bytes = utf8encoder.encode(text);

    const record = { 
        'type': RECORD.EDIT, 
        'message_id': message_id,
        'size': RECORD_FIXED_SIZE + bytes.byteLength,
        'text': text,
    };

    _enqueue(channel_id, record, skip_push);
}

function enqueue_reply(channel_id, reply_to, skip_push = false) {
    const record = {
        'type': RECORD.REPLY,
        'size': RECORD_FIXED_SIZE,
        'message_id': -1,
        'reply_to': reply_to,
    };
    _enqueue(channel_id, record, skip_push);
}

function enqueue_reaction_add(channel_id, message_id, reaction_id) {
    const me = ls_get_me();
    const record = {
        'type': RECORD.REACTION_ADD,
        'size': RECORD_FIXED_SIZE,
        'message_id': message_id,
        'reaction_id': reaction_id,
        'author_id': me.user_id,
    };
    _enqueue(channel_id, record);
}

function enqueue_reaction_remove(channel_id, message_id, reaction_id) {
    const me = ls_get_me();
    const record = {
        'type': RECORD.REACTION_REMOVE,
        'size': RECORD_FIXED_SIZE,
        'message_id': message_id,
        'reaction_id': reaction_id,
        'author_id': me.user_id,
    };
    _enqueue(channel_id, record);
}

function enqueue_message_pin(channel_id, message_id) {
    const record = {
        'type': RECORD.PIN,
        'size': RECORD_FIXED_SIZE,
        'message_id': message_id,
    };
    _enqueue(channel_id, record);
}

function enqueue_attach(channel_id, message_id, file, skip_push = false) {
    const bytes = utf8encoder.encode(file.name);
    
    const record = {
        'type': RECORD.ATTACH,
        'size': RECORD_FIXED_SIZE + bytes.byteLength,
        'message_id': message_id,
        'file_id': file.id,
        'file_ext': file.ext,
    };

    if ('width' in file && 'height' in file) {
        record.width = file.width;
        record.height = file.height;
    } else if ('name' in file) {
        record.file_name = file.name;
    } else {
        record.file_name = record.file_id;
    }

    _enqueue(channel_id, record, skip_push);
}

function enqueue_invite_user_to_channel(user_id, channel_id, skip_push = false) {
    const record = {
        'type': RECORD.USER_JOINED,
        'size': RECORD_FIXED_SIZE,
        'user_id': user_id,
        'timestamp': get_timestamp(),
    }

    _enqueue(channel_id, record, skip_push);
}

function enqueue_leave_channel(user_id, channel_id, skip_push = false) {
    const record = {
        'type': RECORD.USER_LEFT,
        'size': RECORD_FIXED_SIZE,
        'user_id': user_id,
        'timestamp': get_timestamp(),
    }

    _enqueue(channel_id, record, skip_push);
}

function enqueue_change_title(channel_id, new_title, skip_push = false) {
    const bytes = utf8encoder.encode(new_title);
    const me = ls_get_me();

    const record = {
        'type': RECORD.TITLE_CHANGED,
        'size': RECORD_FIXED_SIZE + bytes.byteLength,
        'user_id': me.user_id,
        'length': bytes.byteLength,
        'timestamp': get_timestamp(),
        'text': new_title,
    };

    _enqueue(channel_id, record, skip_push);
}

function try_send(e) {
    const current_channel_id = ls_get_current_channel_id();
    e.preventDefault();

    if (active_uploads > 0) {
        return;
    }

    const msg = divs['message-input'].value.trim();

    if (msg.length > 0 || attaching_files.length > 0) {
        clear_message_input(divs['message-input']);

        hide_reaction_window();

        if (editing_index !== null) {
            // Edit
            enqueue_edit(current_channel_id, editing_index, msg, true);
            divs['message-input-additions'].classList.add('dhide');
        } else if (replying_index !== null) {
            // Reply
            enqueue_message(current_channel_id, null, msg, true);
            enqueue_reply(current_channel_id, replying_index, true);
        }

        if (attaching_files.length > 0) {
            // Attach (text is going to be a separate message)

            if (replying_index === null && editing_index === null) {
                // NOTE(aolo2): we allow just attaches as replies, and also attaching to old messages.
                // This means that we should create an empty message for the attach only if we are not
                // replying or editing
                enqueue_message(current_channel_id, null, msg, true);
            }

            let attach_index = -1;

            if (editing_index !== null) {
                attach_index = editing_index;
            }

            for (const file of attaching_files) {
                enqueue_attach(current_channel_id, attach_index, file, true);
            }
        } else if (editing_index === null && replying_index === null) {
            // Text message

            const me = ls_get_me();
            const tmp_message = {
                'type': RECORD.MESSAGE,
                'channel_id': current_channel_id,
                'author_id': me.user_id,
                'timestamp': Date.now() / 1000,
                'text': msg,
                'reply_to': -1,
                'thread_id': thread_opened ? opened_thread_id : -1
            };
            
            if (!(current_channel_id in unconfirmed_messages)) {
                unconfirmed_messages[current_channel_id] = 0;
            }

            if (!(current_channel_id in confirmed_messages)) {
                confirmed_messages[current_channel_id] = 0;
            }

            unconfirmed_messages[current_channel_id]++; 
            channel_scroller.add_tmp(tmp_message, confirmed_messages[current_channel_id] + unconfirmed_messages[current_channel_id]);

            enqueue_message(current_channel_id, null, msg, true);

            channel_scroller.turn_on_follow_mode();
        }

        cancel_attach();
        cancel_reply();
        cancel_edit();
    }

    push_unsent(current_channel_id);
}

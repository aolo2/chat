const RECORD = {
    'CORRUPTED': -1,
    
    'MESSAGE': 0,
    'DELETE': 1,
    'EDIT': 2,
    'REPLY': 3,
    'REACTION_ADD': 4,
    'REACTION_REMOVE': 5,
    'PIN': 6,
    'ATTACH': 7,

    '_LEGACY_USER_LEFT': 8,
    '_LEGACY_USER_JOINED': 9,

    'USER_LEFT': 100,
    'USER_JOINED': 101,
    'TITLE_CHANGED': 102,
};

const USER_STATUS = {
    OFFLINE: 0,
    ONLINE: 1,
    AWAY: 2,
    BUSY: 3
};

const CHANNEL_FLAGS = {
    'DM': 0x1,
};

function deserializer_create(buffer, dataview) {
    return {
        'offset': 1, // first byte is ALWAYS the opcode
        'size': buffer.byteLength,
        'buffer': buffer,
        'view': dataview,
        'strview': new Uint8Array(buffer),
    };
}

function deserializer_u8(d) {
    const value = d.view.getUint8(d.offset);
    d.offset += 1;
    return value;
}

function deserializer_u16(d) {
    const value = d.view.getUint16(d.offset, true);
    d.offset += 2;
    return value;
}

function deserializer_u32(d) {
    const value = d.view.getUint32(d.offset, true);
    d.offset += 4;
    return value;
}

function deserializer_bigint64(d) {
    const value = d.view.getBigUint64(d.offset, true);
    d.offset += 8;
    return value; // NOTE: this LOOSES precision if the BigInt is large enough (e.g. file_id)
}

function deserializer_text(d, length) {
    const bytes = new Uint8Array(d.buffer, d.offset, length);
    const text = utf8decoder.decode(bytes);
    d.offset += length;
    return text;
}

function deserializer_advance(d, by) {
    if (d.offset + by > d.size) {
        console.error('Value passed to deserializer_advance is too large', by);
        return;
    }

    d.offset += by;
}

function deserializer_set(d, offset) {
    if (offset > d.size) {
        console.error('Offset passed to deserializer_set is too large', offset);
        return;
    }

    d.offset = offset;
}

function deserialize_record(d, record_type, record_size) {
    const record = {};
    const starting_offset = d.offset;
    const message_id = deserializer_u32(d);

    record.type = record_type;
    record.size = record_size;
    record.message_id = message_id;
    let go_to_record_data = () => deserializer_set(d, starting_offset + RECORD_FIXED_SIZE - (2 + 1));
    switch (record_type) {
        case RECORD.MESSAGE: {
            const author_id = deserializer_u32(d);
            const timestamp = deserializer_bigint64(d);
            const thread_id = deserializer_u32(d);
            const text_length = deserializer_u32(d);

            go_to_record_data();

            const message_text = deserializer_text(d, text_length);

            record.author_id = author_id;
            record.timestamp = Number(timestamp);
            record.thread_id = thread_id;
            record.text = message_text;

            break;
        }

        case RECORD.PIN:
        case RECORD.DELETE: {
            break;
        }

        case RECORD.EDIT: {
            const edit_length = deserializer_u32(d);
            go_to_record_data();
            const edit_text = deserializer_text(d, edit_length);

            record.text = edit_text;

            break;
        }

        case RECORD.REPLY: {
            const reply_to = deserializer_u32(d);

            record.reply_to = reply_to;

            break;
        }

        case RECORD.REACTION_ADD:
        case RECORD.REACTION_REMOVE: {
            const reaction_id = deserializer_u32(d);
            const author_id = deserializer_u32(d);

            record.reaction_id = reaction_id;
            record.author_id = author_id;

            break;
        }

        case RECORD.ATTACH: {
            const file_id = deserializer_bigint64(d);
            const file_ext = deserializer_u32(d);

            record.id = file_id.toString(16);
            record.ext = file_ext;

            if (is_supported_image(record.ext)) {
                const image_width = deserializer_u16(d);
                const image_height = deserializer_u16(d);

                if (image_width > 0 && image_height > 0) {
                    record.width = image_width;
                    record.height = image_height;
                } else {
                    record.ext = ATTACHMENT_TYPE.EXT_OTHER;
                    record.name = record.id;
                }
            } else {
                const name_length = deserializer_u16(d);
                go_to_record_data();
                const file_name = deserializer_text(d, name_length);
                record.name = file_name;
                // console.log(file_name);
            }

            break;
        }

        case RECORD._LEGACY_USER_LEFT:
        case RECORD._LEGACY_USER_JOINED:
        case RECORD.USER_LEFT:
        case RECORD.USER_JOINED: {
            const user_id = deserializer_u32(d);
            const timestamp = deserializer_bigint64(d); 
            record.user_id = user_id;
            record.timestamp = Number(timestamp);
            break;
        }

        case RECORD.TITLE_CHANGED: {
            const user_id = deserializer_u32(d);
            const timestamp = deserializer_bigint64(d);
            record.user_id = user_id;
            record.timestamp = Number(timestamp);
            const title_length = deserializer_u32(d);
            go_to_record_data();
            record.title = deserializer_text(d, title_length);
            break;
        }

        default: {
            const author_id = 0;
            const timestamp = 0; 

            record.type = RECORD.CORRUPTED;
            record.author_id = 0;
            record.timestamp = 0;
            record.text = '>> corrupted message <<';

            console.debug('Unhandled record type in deserialize_record', record.type);
            
            d.offset = starting_offset;

            return record;
        }
    }

    return record;
}

function handle_init(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const me_id = deserializer_u32(d);
    const cu_count = deserializer_u16(d);
    const users_count = deserializer_u32(d);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Got an INIT");
    }

    channels = [];

    for (let i = 0; i < cu_count; ++i) {
        const channel_id = deserializer_u32(d);
        const sn = deserializer_u32(d);
        const first_unseen = deserializer_u32(d);

        if (CONFIG_DEBUG_PRINT) {
            console.debug("Channel id =", channel_id, "SN =", sn);
        }

        let channel = ls_get_channel(channel_id);

        if (!channel) {
            channel = {
                'id': channel_id,
                'have_info': false,
                'server': {'client_sn': sn},
                // If we have no info on this channell, then accept the client.sn value from the server to get the same base value
                'client': {'client_sn': sn, 'first_unseen': first_unseen, 'queue': [], 'timeouts': {}}, // TODO: rename to SN
                'users': [],
            }
        } else {
            channel.server.client_sn = sn;
            channel.client.timeouts = {};
            channel.have_info = false;
        }

        if (channel.client.client_sn < channel.server.client_sn) {
            // NOTE(aolo2): we have probably sent messages from another device/browser.
            // Accept the servers version
            channel.server.client_sn = channel.client.client_sn;
        }

        if (first_unseen > channel.client.first_unseen) {
            // NOTE(aolo2): we have seen these messages from another session of ours
            channel.client.first_unseen = first_unseen;
        } else if (channel.client.first_unseen > first_unseen) {
            websocket_send_seen(channel_id, channel.client.first_unseen);
        }

        const last_real_message_index = last_nondeleted_message_index(channel_id);
        if (last_real_message_index !== null && first_unseen <= last_real_message_index) {
            notification_new_unread(true);
        }

        ls_set_channel(channel);

        channels.push(channel);

        if (!(channel_id in messages)) {
            messages[channel_id] = [];
        }
    }

    const users_statuses = {};
    for (let i = 0; i < users_count; ++i) {
        const user_id = deserializer_u32(d);
        const last_online = Number(deserializer_bigint64(d));
        const status = deserializer_u8(d);
        users_statuses[user_id] = { last_online, status };
    }

    const blocks = [];

    for (let i = 0; i < users_count * 2; ++i) { // login + name
        const block_id = deserializer_u32(d);
        const block_length = deserializer_u32(d);
        const block_data = deserializer_text(d, block_length);

        blocks.push({
            'id': block_id,
            'data': block_data
        });
    }

    const channel_list = channels.map(c => c.id);
    ls_set_channel_list(channel_list);

    const users = [];

    for (let i = 0; i < users_count; ++i) {
        const user_id = deserializer_u32(d);
        const avatar_id = deserializer_bigint64(d);

        deserializer_advance(d, 128); // pass hash
        
        const name_block = deserializer_u32(d);
        const login_block = deserializer_u32(d);
        const blob_block = deserializer_u32(d);

        const name = get_block_data(blocks, name_block) || 'ERROR';
        const login = get_block_data(blocks, login_block) || 'ERROR';

        let user = {
            'id': user_id,
            'name': name,
            'login': login,
            'avatar': avatar_id.toString(16)
        };

        if (user_id in users_statuses) {
            user.last_online = users_statuses[user_id].last_online;
            user.status = users_statuses[user_id].status;
        }

        users.push(user);

        if (user_id === me_id) {
            const me = ls_get_me();
            me.display = name;
            me.login = login;
            me.blob = blob_block;
            me.avatar = avatar_id.toString(16);
            ls_set_me(me);
            const username_element = find('my-username-topleft-text');
            if (username_element) {
                username_element.innerHTML = name;
            }
        }
    }

    ls_set_users(users);


    for (const channel of channels) {
        // if (!(channel.id in unconfirmed_messages)) {
        //     unconfirmed_messages[channel.id] = 0;
        // }

        // if (!(channel.id in confirmed_messages)) {
        //     confirmed_messages[channel.id] = 0;
        // }

        websocket_send_request_channel_info(channel.id);

        if (channel.client.queue.length > 0) {
            push_unsent(channel.id);

            // This only happens after successfull init any way, so the messages get sent right away, and
            // there is no need for a placeholder

            // for (const record of channel.client.queue) {
            //     unconfirmed_messages[channel.id]++; 
            //     scroller_add_tmp(record, confirmed_messages[channel.id] + unconfirmed_messages[channel.id]);
            // }
        }
    }

    settings_init();
}

function handle_ack(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const channel_id = deserializer_u32(d);
    const sn = deserializer_u32(d);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Got an ACK for channel", channel_id, "sn =", sn);
    }

    const channel = ls_get_channel(channel_id);
    if (!channel) {
        console.error('Received ACK for a non-existent channel');
        return;
    }

    channel.server.client_sn = sn;

    const unacked_messages = []; // TODO @speed: do not create a new array, use a slice
    const leave_last_n = channel.client.client_sn - sn;
    const queue = channel.client.queue;

    for (let i = 0; i < queue.length; ++i) {
        if (i >= queue.length - leave_last_n) {
            unacked_messages.push(queue[i]);
        }
    }

    channel.client.queue = unacked_messages;

    ls_set_channel(channel);

    if (sn in channel.client.timeouts) {
        clearTimeout(channel.client.timeouts[sn]);
    }

    // sent_play();
}

// New messages (from different users or my own)
function handle_syn(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const channel_id = deserializer_u32(d);
    const sn = deserializer_u32(d);
    const total_server_messages = deserializer_u32(d);
    const initial_syn = deserializer_u8(d);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Got a SYN for channel", channel_id, "sn =", sn, "nmessages =", total_server_messages);
    }

    const me = ls_get_me();
    const channel = ls_get_channel(channel_id);
    if (!channel) {
        console.error('Received SYN for a non-existent channel', channel_id);
        return;
    }

    // TODO: srv_sn < our sn. means wipe and try again

    const first_open = (messages[channel_id].length === 0);

    // See comment on the server (in recv_sync)
    const we_expect_messages = (sn - messages[channel_id].length);
    const starting_message = total_server_messages - we_expect_messages;

    let user_visible_messages = 0;
    let last_visible_message = null;
    let last_mine = -1;

    for (let i = 0; i < total_server_messages; ++i) {
        const record_offset = d.offset;
        const record_type = deserializer_u8(d);
        const record_size = deserializer_u16(d);

        if (i >= starting_message) {
            const record = deserialize_record(d, record_type, record_size);

            if (record) {
                if (record.type === RECORD.MESSAGE) {
                    const author_id = record.author_id;
                    const idx = typing_users.find((val) => val.channel_id === channel_id && val.user_id === author_id);

                    if (idx) {
                        typing_users.splice(idx, 1);
                        l_update_typing_message();
                    }

                    if (record.author_id !== me.user_id) {
                        ++user_visible_messages;
                        last_visible_message = record;
                    } else {
                        last_mine = messages[channel_id].length;

                        if (!(channel_id in unconfirmed_messages)) {
                            unconfirmed_messages[channel_id] = 0;
                        }

                        if (!(channel_id in confirmed_messages)) {
                            confirmed_messages[channel_id] = 0;
                        }


                        confirmed_messages[channel_id]++;

                        if (unconfirmed_messages[channel_id] > 0) unconfirmed_messages[channel_id]--;

                        channel_scroller.remove_tmp(channel_id, confirmed_messages[channel_id]);
                        thread_content_block.scroller.remove_tmp(channel_id, confirmed_messages[channel_id]);
                    }
                }

                messages[channel_id].push(record);

                apply_decoration(channel, record);
            }
        }

        deserializer_set(d, record_offset + record_size);
    }

    if (last_mine + 1 > channel.client.first_unseen) {
        // If I got my own message then surely I have seen
        // at least until there on some other device
        channel.client.first_unseen = last_mine + 1;
    }

    ls_set_channel(channel);

    if (channel.have_info) {
        redraw_channel(channel);
    }
    
    websocket_send_ack(channel_id, sn);

    if (first_open) {
        channel_scroller.establish_basepoint(messages[channel_id].length);
        thread_content_block.scroller.establish_basepoint(messages[channel_id].length);
    }

    channel_scroller.append(channel_id, first_open);
    thread_content_block.scroller.append(channel_id, first_open);

    if (first_open) {
        imm_storage_sync(channel_id);
    }

    // This will play notification sounds if you write to a different channel
    // from your other device, but whatever (TODO)
    const last_real_message_index = last_nondeleted_message_index(channel.id);
    if (last_real_message_index !== null && channel.client.first_unseen <= last_real_message_index) {
        if (!initial_syn) {
            notification_push(last_visible_message);
        }

        notification_new_unread(last_visible_message);
        move_channel_to_top(channel_id);
    }
}

function handle_invited(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const channel_id = deserializer_u32(d);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Got an INVITE to channel", channel_id);
    }

    const channel = {
        'id': channel_id,
        'server': { 'client_sn': 0 },
        'client': { 'client_sn': 0, 'first_unseen': 0, 'queue': [], 'timeouts': {} }, // TODO: rename to SN
    }

    ls_set_channel(channel);
    ls_join_channel(channel_id);
    channels.push(channel);

    if (!(channel_id in messages)) {
        messages[channel_id] = [];
    }

    websocket_send_request_channel_info(channel_id);
}

function handle_channel_info(buffer, dataview) {
    --n_requested_channels;

    if (n_requested_channels === 0) {
        channel_list_spinner.stop();
        divs['channel-list'].classList.remove('tdisabled');
        divs['channels-spinner'].classList.add('dhide');
        divs['sidebar'].classList.add('animation-ready');
    }

    const d = deserializer_create(buffer, dataview);
    const title_length = deserializer_u8(d);
    const avatar_id = deserializer_bigint64(d);
    const channel_id = deserializer_u32(d);
    const channel_flags = deserializer_u32(d);
    const cu_count = deserializer_u16(d);

    if (CONFIG_DEBUG_PRINT) {
      console.debug("Got CHANNEL_INFO for channel", channel_id);
    }

    let title;
    if (title_length > 0) {
        title = deserializer_text(d, title_length);
    } else {
        title = '';
    }

    const channel = ls_get_channel(channel_id);
    if (!channel) {
        console.error('Received channel info for a non-existent channel', channel_id);
        return;
    }

    channel.is_dm = (channel_flags & CHANNEL_FLAGS.DM) === 1;
    channel.users = [];
    channel.have_info = true;
    channel.avatar_id = avatar_id.toString(16);

    for (let i = 0; i < cu_count; ++i) {
        const user_id = deserializer_u32(d);
        const first_unseen = deserializer_u32(d);
        channel.users.push({'id': user_id, 'first_unseen': first_unseen});
    }

    channel.title = title;
    ls_set_channel(channel);
    redraw_channel(channel);

    const current_channel_id = ls_get_current_channel_id();

    if (channel_id === current_channel_id) {
        switch_to_channel(current_channel_id);
    }
}

function handle_typing_msg(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const channel_id = deserializer_u32(d);
    const user_id = deserializer_u32(d);

    const ctu = typing_users.find((val) => {
        return val.channel_id === channel_id && val.user_id === user_id;
    });

    if (ctu) {
        ctu.recv_time = Date.now();
    } else {
        let user = ls_find_user(user_id);
        if (!user) return;
        typing_users.push({
            'channel_id': channel_id,
            'user_id': user_id,
            'user_login': user.login,
            'recv_time': Date.now(),
        });
        l_update_typing_message();
    }
    if (!interval_refresh_typing_users_list) {
        interval_refresh_typing_users_list = setInterval(refresh_typing_users_list, TYPING_DURATION * 0.66);
    }
}

function handle_user_seen(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const user_id = deserializer_u32(d);
    const channel_id = deserializer_u32(d);
    const first_unseen = deserializer_u32(d);

    if (CONFIG_DEBUG_PRINT) {
        console.debug(`User ${user_id} in channel ${channel_id} has seen until ${first_unseen}`);
    }

    const channel = ls_get_channel(channel_id);
    if (channel) {
        for (const user of channel.users) {
            if (user.id === user_id) {
                user.first_unseen = first_unseen;
                break;
            }
        }

        const me = ls_get_me();

        if (user_id === me.user_id) {
            channel.client.first_unseen = first_unseen;
        }

        ls_set_channel(channel);
        channel_scroller.set_seen(channel_id, first_unseen);
        redraw_channel(channel);
    }
}

function handle_pushpoll(buffer, dataview) {}

function handle_status_change(buffer, dataview) {
    const d = deserializer_create(buffer, dataview);
    const user_id = deserializer_u32(d);
    const last_online = deserializer_bigint64(d);
    const user_status = deserializer_u8(d);
    const user = ls_find_user(user_id);

    if (user && user.status !== user_status) {
        user.status = user_status;
        user.last_online = Number(last_online);

        ls_set_user(user);
        update_status(user);

        const current_channel_id = ls_get_current_channel_id();
        const channel = ls_get_channel(current_channel_id);
        const members_popup = document.querySelector('.channel-members-list');

        if (current_channel_id !== null && channel && members_popup) {
            const total_in_channel = channel.users.length;
            let online_users = 0;

            for (const u of channel.users) {
                const user = ls_find_user(u.id);
                if (user && user.status === USER_STATUS.ONLINE) {
                    ++online_users;
                }
            }

            const online_item = document.querySelector('.popup-member-list-header');
            if (online_item) {
                online_item.innerText = `${total_in_channel} members, ${online_users} online`;
            }
        }
    }
}

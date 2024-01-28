const utf8decoder = new TextDecoder();
const utf8encoder = new TextEncoder();

let ws = null;
let ws_connect_timer = null;
let ws_reconnect_timeout = CONFIG_WS_INITIAL_TIMEOUT;
let ws_user_handlers = {};
let ws_toast = null;

const WS_MESSAGE_TYPE = {
    CLIENT: {
        AUTH: 0,
        INIT: 1,
        LOGOUT: 2,
        SYNC: 3,
        ACK: 4,
        SEEN: 5,

        ADD_USER: 10,
        ADD_CHANNEL: 11,
        REMOVE_USER: 13,
        REMOVE_CHANNEL: 14,
        REMOTE_CHANNEL_USER: 15,
        ADD_DIRECT: 16,
        IS_TYPING: 17,

        REQUEST_CHANNEL_INFO: 20,

        SET_USER_AVATAR: 30,
        SET_CHANNEL_AVATAR: 31,
        SET_CHANNEL_NAME: 32,

        CHANGE_PASSWORD: 40,

        SAVE_UTF8: 50,
        REQUEST_UTF8: 51,
    },

    SERVER: {
        AUTH_SUCCESS: 100,
        AUTH_FAIL: 101,
        INIT: 102,
        ACK: 103,
        SYNC: 104,
        INVITED: 105,
        CHANNEL_INFO: 106,
        USER_STATUS: 107,
        USER_SEEN: 108,
        PUSHPOLL: 109,
        USER_IS_TYPING: 110,
        UTF8_BLOB_SAVED: 200,
        UTF8_BLOB_DATA: 201,
    },
};

function websocket_init() {
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.AUTH_FAIL,       l_logout);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.INIT,            handle_init);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.ACK,             handle_ack);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.SYNC,            handle_syn);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.INVITED,         handle_invited);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.CHANNEL_INFO,    handle_channel_info);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.USER_SEEN,       handle_user_seen);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.PUSHPOLL,        handle_pushpoll);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.USER_STATUS,     handle_status_change);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.USER_IS_TYPING,  handle_typing_msg);
}

function websocket_connect(address, on_open = null) {
    ws = new WebSocket(address);

    const toast_timer = setTimeout(() => {
        find('ws-toast-text').innerHTML = 'Connecting to the<br>websocket server...';
        find('ws-toast').classList.remove('hidden');
    }, CONFIG_TOAST_DELAY_MS);

    ws.addEventListener('open', () => {
        find('ws-toast').classList.add('hidden');
        clearTimeout(ws_connect_timer);
        clearTimeout(toast_timer);
        ws_connect_timer = null;
        ws_reconnect_timeout = CONFIG_WS_INITIAL_TIMEOUT;
        if (on_open) {
            on_open();
        }
    });
    
    ws.addEventListener('message', websocket_handle_message);
    ws.addEventListener('error', () => { ws.close(); });
    ws.addEventListener('close', () => {
        ws_connect_timer = setTimeout(() => websocket_connect(address, on_open), ws_reconnect_timeout);
        ws_reconnect_timeout = Math.ceil(ws_reconnect_timeout * 1.5);
    });
}

function websocket_register_handler(message_type, handler) {
    if (!(message_type in ws_user_handlers)) {
        ws_user_handlers[message_type] = [];
    }

    ws_user_handlers[message_type].push(handler);
}

///////////////////////////////////////
//////////////// SEND /////////////////
///////////////////////////////////////
function websocket_send_auth(login, password) {
    const login_bytes = utf8encoder.encode(login);
    const password_bytes = utf8encoder.encode(password);

    const message_data = new ArrayBuffer(login_bytes.byteLength + password_bytes.byteLength + 2);
    const view = new DataView(message_data);
    const strview = new Uint8Array(message_data);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.AUTH)
    view.setUint8(1, login_bytes.byteLength)

    strview.set(login_bytes, 2);
    strview.set(password_bytes, 2 + login_bytes.byteLength);

    if (ws) ws.send(message_data);
}

function websocket_send_logout() {
    const message_data = new ArrayBuffer(1);
    const view = new DataView(message_data);
    
    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.LOGOUT);

    if (ws) ws.send(message_data);
}

function websocket_send_init(session_id_string, channel_ids) {
    const nchannels = channel_ids.length;
    const session_id = BigInt(session_id_string);
    const message_data = new ArrayBuffer(1 + 8 + 4 + (4 + 4) * nchannels); // opcode + session_id + nchannels + (channel_id + sn) * nchannels
    const view = new DataView(message_data);

	if (CONFIG_DEBUG_PRINT) {
		console.debug("Sending INIT");
	}

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.INIT);
    view.setBigUint64(1, session_id, true);
    view.setUint32(8 + 1, nchannels, true);

    let offset = 1 + 8 + 4;

    for (const channel_id of channel_ids) {
        const channel = ls_get_channel(channel_id);

        view.setUint32(offset, channel.id, true);
        view.setUint32(offset + 4, messages[channel_id].length, true);

		if (CONFIG_DEBUG_PRINT) {
			console.debug("Channel id =", channel_id, "SN =", messages[channel_id].length);
		}

        offset += 4 + 4;
    }

    if (ws) ws.send(message_data);
}

function websocket_send_ack(channel_id, sn) {
    const message_data = new ArrayBuffer(1 + 4 + 4);
    const view = new DataView(message_data);

	if (CONFIG_DEBUG_PRINT) {
		console.debug("Sending ACK for channel", channel_id, "sn =", sn);
	}

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.ACK);
    view.setUint32(1, channel_id, true);
    view.setUint32(1 + 4, sn, true);

    if (ws) ws.send(message_data);
}

function websocket_send_seen(channel_id, first_unseen) {
    const message_data = new ArrayBuffer(1 + 4 + 4);
    const view = new DataView(message_data);

    if (CONFIG_DEBUG_PRINT) {
        console.debug(`Sending SEEN = ${first_unseen} for channel ${channel_id}`);
    }

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.SEEN);
    view.setUint32(1, channel_id, true);
    view.setUint32(1 + 4, first_unseen, true);

    if (ws) ws.send(message_data);
}

function websocket_send_request_channel_info(channel_id) {
    const message_data = new ArrayBuffer(1 + 4);
    const view = new DataView(message_data);

	if (CONFIG_DEBUG_PRINT) {
		console.debug("Sending REQUEST_CHANNEL_INFO for channel", channel_id);
	}

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.REQUEST_CHANNEL_INFO);
    view.setUint32(1, channel_id, true);

    if (ws) {
        ws.send(message_data);
        ++n_requested_channels;
    }
}

function websocket_send_user_is_typing(channel_id) {
    const message_data = new ArrayBuffer(1 + 4);
    const view = new DataView(message_data);

	if (CONFIG_DEBUG_PRINT) {
		console.debug("Sending CLIENT_TYPING_MSG for channel", channel_id);
	}

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.IS_TYPING);
    view.setUint32(1, channel_id, true);

    if (ws) ws.send(message_data);
}

function websocket_send_set_user_avatar(avatar_id_string) {
    const message_data = new ArrayBuffer(1 + 8);
    const view = new DataView(message_data);
    const avatar_id = BigInt(avatar_id_string);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Sending CLIENT_SET_USER_AVATAR");
    }

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.SET_USER_AVATAR);
    view.setBigUint64(1, avatar_id, true);

    if (ws) ws.send(message_data);
}

function websocket_send_set_channel_avatar(channel_id, avatar_id_string) {
    const message_data = new ArrayBuffer(1 + 4 + 8);
    const view = new DataView(message_data);
    
    const avatar_id = BigInt(avatar_id_string);

    if (CONFIG_DEBUG_PRINT) {
        console.debug("Sending CLIENT_SET_USER_AVATAR");
    }

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.SET_CHANNEL_AVATAR);
    view.setUint32(1, channel_id, true);
    view.setBigUint64(1 + 4, avatar_id, true);

    if (ws) ws.send(message_data);
}

function websocket_send_change_password(password) {
    const password_bytes = utf8encoder.encode(password);
    const password_length = password_bytes.byteLength;

    const message_data = new ArrayBuffer(1 + 1 + password_length);
    const view = new DataView(message_data);
    const strview = new Uint8Array(message_data);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.CHANGE_PASSWORD);
    view.setUint8(1, password_length);
    
    strview.set(password_bytes, 1 + 1);

    if (ws) ws.send(message_data);
}

function websocket_save_utf8(data, callback) {
    const bytes = utf8encoder.encode(data);
    const length = bytes.byteLength;

    const message_data = new ArrayBuffer(1 + 4 + 4 + length);
    const view = new DataView(message_data);
    const strview = new Uint8Array(message_data);

    const nonce = random_int(1024, 1024 * 1024 * 1024);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.SAVE_UTF8);
    view.setUint32(1, nonce, true);
    view.setUint32(1 + 4, length, true);
    strview.set(bytes, 1 + 4 + 4);

    ws_user_handlers[nonce] = callback;

    if (ws) ws.send(message_data);
}

function websocket_request_utf8(callback) {
    const message_data = new ArrayBuffer(1 + 4 + 4);
    const view = new DataView(message_data);

    const nonce = random_int(1024, 1024 * 1024 * 1024);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.REQUEST_UTF8);
    view.setUint32(1, nonce, true);

    ws_user_handlers[nonce] = callback;

    if (ws) ws.send(message_data);
}

function tmp_add_channel(title) {
    if (title.length === 0) return;

    const title_bytes = utf8encoder.encode(title);
    const title_length = title_bytes.byteLength;

    const message_data = new ArrayBuffer(1 + 1 + title_length);
    const view = new DataView(message_data);
    const strview = new Uint8Array(message_data);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.ADD_CHANNEL);
    view.setUint8(1, title_length);
    
    strview.set(title_bytes, 1 + 1);

    ws.send(message_data);
}

function tmp_add_direct(user_id) {
    // TODO: maybe merge with "add channel"?
    const message_data = new ArrayBuffer(1 + 4);
    const view = new DataView(message_data);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.ADD_DIRECT);
    view.setUint32(1, user_id, true);

    ws.send(message_data);
}

function handle_data(message_data) {
    const view = new DataView(message_data);
    const opcode = view.getUint8(0);

    if (opcode === WS_MESSAGE_TYPE.SERVER.UTF8_BLOB_SAVED ||
        opcode === WS_MESSAGE_TYPE.SERVER.UTF8_BLOB_DATA) {
        const nonce = view.getUint32(1, true).toString();
        if (nonce in ws_user_handlers) {
           if (CONFIG_DEBUG_PRINT) {
                console.debug(`Firing one-time handler for nonce ${nonce}`);
            }
            ws_user_handlers[nonce](message_data, view);
            delete ws_user_handlers[nonce];
        } else {
            console.error(`No handler for nonce ${nonce}`);
        }

        return;
    }

    if (opcode in ws_user_handlers) {
        for (const handler of ws_user_handlers[opcode]) {
            handler(message_data, view);
        }
    } else {
        console.log(`[WARNING] Unhandled websocket message! (opcode = ${opcode})`);
    }
}

async function websocket_handle_message(event) {
    const data = event.data;

    if ('arrayBuffer' in data) {
        const message_data = await data.arrayBuffer();
        handle_data(message_data);
    } else {
        /* For all my Safari < 14 bros out there */
        const reader = new FileReader();
        reader.onload = async (e) => {
            const message_data = e.target.result;
            handle_data(message_data);
        };

        reader.readAsArrayBuffer(data);
    }
}
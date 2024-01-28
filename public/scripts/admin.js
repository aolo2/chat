function tmp_add_user(login, password, display) {
    if (login.length === 0 || password.length === 0 || display.length === 0) return;

    console.log(login, password, display);

    const login_bytes = utf8encoder.encode(login);
    const password_bytes = utf8encoder.encode(password);
    const display_bytes = utf8encoder.encode(display);

    const message_data = new ArrayBuffer(
        1 + 1 + 1 + 1 + login_bytes.byteLength 
        + password_bytes.byteLength 
        + display_bytes.byteLength
    );

    const view = new DataView(message_data);
    const strview = new Uint8Array(message_data);

    view.setUint8(0, WS_MESSAGE_TYPE.CLIENT.ADD_USER);
    view.setUint8(1, login_bytes.byteLength);
    view.setUint8(1 + 1, password_bytes.byteLength);
    view.setUint8(1 + 1 + 1, display_bytes.byteLength);

    strview.set(login_bytes,    1 + 1 + 1 + 1);
    strview.set(password_bytes, 1 + 1 + 1 + 1 + login_bytes.byteLength);
    strview.set(display_bytes,  1 + 1 + 1 + 1 + login_bytes.byteLength + password_bytes.byteLength);

    ws.send(message_data);

    document.getElementById('text').innerHTML = 'success';    
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

// function tmp_send_message(channel_id, text) {
//     channel_id = parseInt(channel_id);

//     if (isNaN(channel_id) || text.length === 0) return;

    
// }

function tmp_login(login, password) {
    if (login.length > 0 && password.length > 0) {
        websocket_send_auth(login, password);
    }
}

let session_id = null;
let user_id = null;

function admin_auth_success(buffer, dataview) {
    session_id = dataview.getBigUint64(1, true);
    user_id = dataview.getUint32(9, true);

    const status = find('login-status');
    status.innerHTML = 'LOGGED IN';
    status.style.color = 'green';
}

function admin_auth_fail() {
    const status = find('login-status');
    status.innerHTML = 'LOGGED OUT';
    status.style.color = 'red';
}

function tmp_init_buttons() {
    try {
        const b_add_channel = find('tmp-add-channel-button');
        const t_add_channel = find('tmp-add-channel');

        const b_add_user = find('tmp-add-user-button');
        const t_add_user_login = find('tmp-add-user-login');
        const t_add_user_pass = find('tmp-add-user-password');
        const t_add_user_display = find('tmp-add-user-display');

        // const b_add_cu = find('tmp-add-channel-user-button');
        // const t_add_cu_user = find('tmp-add-user-channel-user');
        // const t_add_cu_channel = find('tmp-add-user-channel-channel');

        const b_login = find('tmp-login');
        const t_login_login = find('tmp-login-login');
        const t_login_password = find('tmp-login-password');

        // const b_send_message = find('tmp-send-message-button');
        // const t_send_message_text = find('tmp-send-message-text');
        // const t_send_message_channel = find('tmp-send-message-channel');

//        b_login.addEventListener('click', () => tmp_login(
//            t_login_login.value.trim(),
//            t_login_password.value.trim()
//        ));

//        b_add_channel.addEventListener('click', () => tmp_add_channel(t_add_channel.value.trim()));
        b_add_user.addEventListener('click', () => tmp_add_user(
            t_add_user_login.value.trim(),
            t_add_user_pass.value.trim(),
            t_add_user_display.value.trim()
        ));

        // b_add_cu.addEventListener('click', () => tmp_add_cu(t_add_cu_user.value, t_add_cu_channel.value));
        // b_send_message.addEventListener('click', () => tmp_send_message(t_send_message_channel.value, t_send_message_text.value.trim()));
    } catch (e) {
        console.error(e)
    }
}

document.addEventListener('DOMContentLoaded', () => {
    websocket_connect(CONFIG_WS_URL, () => {
        websocket_register_handler(WS_MESSAGE_TYPE.SERVER.AUTH_FAIL, admin_auth_fail);
        websocket_register_handler(WS_MESSAGE_TYPE.SERVER.AUTH_SUCCESS, admin_auth_success);
    });
    tmp_init_buttons();
}, false);

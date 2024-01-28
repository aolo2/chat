function l_avatar_upload_started(file) {
    const url = URL.createObjectURL(file);
    document.querySelector('#avatar-big-preview').src = url;
}

function l_avatar_upload_complete(file) {
	websocket_send_set_user_avatar(file.id);
}

function settings_init() {
    const uploader = uploader_create(document.getElementById('avatar-upload-hidden-button'), {
        'url_create': '/upload/upload-req',
        'url_status': '/upload/upload-status',
        'url_upload': '/upload/upload-do',
        'on_start':    l_avatar_upload_started,
        'on_complete': l_avatar_upload_complete,
    });

    const me = ls_get_me();
    if (me && me.avatar !== '0') {
        const avatar_element = find('avatar-big-preview');
        if (avatar_element) {
            avatar_element.src = `storage/${me.avatar}-2`;
        }
    }

    const displayname_element = find('settings-me-displayname');
    const login_element = find('settings-me-login');
    if (displayname_element) displayname_element.innerText = me.display;
    if (login_element) login_element.innerText = '@' + me.login;

    restore_settings();
}

function settings_open_avatar_selection_dialog() {
    document.querySelector('#avatar-upload-hidden-button').click();
}

function settings_reset_avatar() {
	websocket_send_set_user_avatar('0');
}

function restore_settings() {
    const me = ls_get_me();
    websocket_request_utf8((buffer, dataview) => {
        const d = deserializer_create(buffer, dataview);
        const nonce = deserializer_u32(d);
        const length = deserializer_u32(d);
        const settings_string = deserializer_text(d, length);
        ls_save_settings(me.blob, settings_string);
    });
}

function save_settings() {
    const settings_data = {'sound': 'off'};
    const settings_string = JSON.stringify(settings_data);
    const me = ls_get_me();
    
    websocket_save_utf8(settings_string, (buffer, dataview) => {
        const d = deserializer_create(buffer, dataview);
        const nonce = deserializer_u32(d);
        const settings_key = deserializer_u32(d);
        ls_save_settings(settings_key, settings_string);
    });
}
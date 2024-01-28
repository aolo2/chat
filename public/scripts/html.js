const divs = {};

let interval_refresh_typing_users_list = null;
let last_typing = 0;
const TYPING_DURATION = 3000;

// { channel_id, user_id, recv_time, user_login }
let typing_users = [];

function l_handle_typing(){
    let now = Date.now();
    if (now - last_typing > TYPING_DURATION * 0.5) {
        last_typing = now;
        const channel_id = ls_get_current_channel_id();
        websocket_send_user_is_typing(channel_id);
    }
}

function l_logout() {
    websocket_send_logout();
    logout();
}

function l_cancel_message() {
    if (editing_index !== null) cancel_edit();
    if (replying_index !== null) cancel_reply();
}

function l_toggle_search() {
    search_toggle(null);
}

function l_pin_jump() {
    if (pinned_index) {
        channel_scroller.jump_to(pinned_index);
    }
}

function l_pin_unpin() {
    enqueue_message_pin(ls_get_current_channel_id(), -1);
}

function l_message_key(e, autocomplete_onkeydown) {
    const cancel = autocomplete_onkeydown(e);
    if (cancel) {
        return;
    }

    if (e.code === 'Enter' && !e.shiftKey) {
        try_send(e);
    } else if (e.code === 'Escape') {
        if (editing_index !== null) cancel_edit();
        if (replying_index !== null) cancel_reply();
    } else if (e.code === 'ArrowUp' && divs['message-input'].value.length === 0) {
        const last_mine = get_last_mine_message();
        if (last_mine !== -1) {
            initiate_edit(last_mine);
        }
    }
}

function l_update_typing_message() {
    const channel_id = ls_get_current_channel_id();
    let typing_in_channel = typing_users.filter(i => i.channel_id === channel_id);
    divs['typing-indicator'].innerHTML = add_typing_user_for_channel(typing_in_channel);
}

function refresh_typing_users_list() {
    const channel_id = ls_get_current_channel_id();
    let now = Date.now();
    let still_typing = typing_users.filter(i => now - i.recv_time < TYPING_DURATION && i.channel_id === channel_id);

    if (still_typing.length !== typing_users.length) {
        typing_users = still_typing;
        l_update_typing_message();
        if (still_typing.length === 0) {
            clearInterval(interval_refresh_typing_users_list);
            interval_refresh_typing_users_list = null;
        }
    }
}

function l_create_channel() {
    divs['sidebar'].classList.remove('shown');
    switch_to_channel(null);
}

function l_show_direct_creation_ui() {
    if (channel_creation_state === 0) {
        find('channel-ui').classList.add('dhide');
        find('chat-step1').classList.add('dhide');
        find('chat-step2').classList.remove('dhide');
        divs['b-chat'].innerHTML = 'Back';
        channel_creation_state = 1;
    } else if (channel_creation_state === 1) {
        find('channel-ui').classList.remove('dhide');
        find('chat-step1').classList.remove('dhide');
        find('chat-step2').classList.add('dhide');
        divs['b-chat'].innerHTML = 'Direct';
        channel_creation_state = 0;
    }
}

function l_show_channel_creation_dialog() {
    if (channel_creation_state === 0) {
        find('chat-ui').classList.add('dhide');
        find('channel-step1').classList.add('dhide');
        find('channel-step2').classList.remove('dhide');
        divs['b-channel'].innerHTML = 'Back';
        channel_creation_state = 1;
    } else if (channel_creation_state === 1) {
        find('chat-ui').classList.remove('dhide');
        find('channel-step1').classList.remove('dhide');
        find('channel-step2').classList.add('dhide');
        divs['b-channel'].innerHTML = 'Channel';
        channel_creation_state = 0;
    }
}

function l_confirm_direct_creation() {
    const login = find('find-chatter').value;
    const user = ls_find_user_by_login(login);
    if (user) {
        tmp_add_direct(user.id);
        find('find-chatter').value = '';

        find('ws-toast-text').innerHTML = 'Channel created';
        find('ws-toast').classList.remove('hidden');

        setTimeout(() => {
            find('ws-toast').classList.add('hidden');
        }, 3000);
    } else {
        console.error('Attempt to create a direct chat with a non-existent user', login);
    }
}

function l_confirm_channel_creation() {
    const title = find('name-channel').value.trim();
    if (title.length > 0) {
        tmp_add_channel(title);

        find('name-channel').value = '';

        find('ws-toast-text').innerHTML = 'Channel created';
        find('ws-toast').classList.remove('hidden');

        setTimeout(() => {
            find('ws-toast').classList.add('hidden');
        }, 3000);
    }
}

function l_show_channel_info() {
    const current_channel_id = ls_get_current_channel_id();
    popup_show(
        '',
        channel_info_html(current_channel_id),
        [
        {
            'class': 'negative', 
            'text': 'Leave channel',
            'action': () => {
                popup_show(
                    'Leave channel', 'Are you sure?', [
                       {
                            'class': 'negative',
                            'text': 'Leave',
                            'action': () => {
                                const me = ls_get_me();
                                enqueue_leave_channel(me.user_id, current_channel_id);

                                const next_item = remove_channel(current_channel_id);

                                if (next_item) {
                                    switch_to_channel(next_item.getAttribute('data-channel-id'));
                                } else {
                                    switch_to_channel(null);
                                }
                            }
                        },
                        {
                            'text': 'cancel'
                        }
                    ]
                );

                return false;
            }
        },
        {
            'class': 'primary', 
            'text': 'Invite members',
            'action': () => {
                popup_show(
                    'Invite members',
                    invite_panel_html(current_channel_id),
                    [{
                        'class': 'primary tdisabled invite-confirm', 
                        'text': 'invite',
                        'action': () => {
                            const channel = ls_get_channel(current_channel_id);
                            for (const user_id of invite_selected) {
                                enqueue_invite_user_to_channel(user_id, current_channel_id);
                                channel.users.push({'id': user_id, 'first_unseen': 0}); // TODO: do we need to fill first_unseen here?
                            }
                            invite_selected.length = 0;
                        }
                    }]
                )

                return false;
            }
        }]
    )
}

function l_redraw_user_autocomplete(e) {
    const query = e.target.value;
    const query_lc = query.toLowerCase();

    find('chat-create-confirm').classList.add('tdisabled');

    if (query.trim().length === 0) {
        divs['user-autocomplete-variants'].innerHTML = '';
        return;
    }

    const users = ls_get_users();
    const filtered = users.filter(u => {
        const name_lc = u.name.toLowerCase();
        const login_lc = u.login.toLowerCase();
        return(name_lc.includes(query_lc) || login_lc.includes(query_lc));
    });

    divs['user-autocomplete-variants'].innerHTML = '';

    for (const user of filtered) {
        const item = user_autocomplete_variant(user);
        make_clickable(item, () => { 
            e.target.value = user.login;
            find('chat-create-confirm').classList.remove('tdisabled');
            divs['user-autocomplete-variants'].innerHTML = '';          
        })
        divs['user-autocomplete-variants'].appendChild(item);
    }
}

function l_submit_file_upload(e) {
    const files = find('selected-file').files;
    if (files.length > 0) {
        file = {
            'name': files[0].name,
            'type': files[0].type === '' ? '.txt' : '',
            'size': files[0].size,
            'data': files[0]
        }
        upload_file(file);
        // progress.bar.style.width = '0%';
        // progress.bar_percents.innerHTML = '';
    }
    return false;
}

function l_show_settings() {
    divs['sidebar'].classList.remove('shown');
    document.querySelectorAll('.content-block').forEach(el => el.classList.add('dhide'));
    document.querySelector('.channel-creation').classList.add('dhide');
    thread_close();
    document.querySelector('.settings').classList.remove('dhide');
}

function l_open_file_picker_dialog() {
    divs['file-upload-hidden-button'].click();
}

function remove_attachment(file_id_hex) {
    // TODO: implement upload cancel in media-server and cancel it here if it's in progress
    //
    // const upload_info = uploader_info(file_id_hex)
    // if (upload_info.in_progress) {
    // uploader_cancel(upload_info);
    // }

    cancelled_files.push(file_id_hex);
    document.querySelector(`.any-attachment[data-file-id="f-${file_id_hex}"]`).remove();

    for (let i = 0; i < attaching_files.length; ++i) {
        if (attaching_files[i].id === file_id_hex) {
            attaching_files.splice(i, 1);
            break;
        }
    }

    if (document.querySelector('#msg-attachments-row .any-attachment') === null) {
        divs['msg-attachments-row'].classList.add('dhide');
    }

    if (document.querySelector('#msg-attachments-files-row .any-attachment') === null) {
        divs['msg-attachments-files-row'].classList.add('dhide');
    }
}

function l_upload_started(file) {
    const file_id_hex = BigInt(file.id).toString(16);

    ++active_uploads;
    refresh_message_send();
    
    if (is_supported_image(uploader_server_ext(file.type))) {
        const url = URL.createObjectURL(file);
        // .any-attachment and .preloader-wrapper for querying both image and file attachments in progress updated
        const item = HTML(`
        <div class="attachment any-attachment" data-file-id="f-${file_id_hex}">
            <div class="attachment-remove" title="Remove attachment"><img draggable="false" src="/static/icons/cancel_333.svg"></div>
            <div class="img-preloader-wrapper preloader-wrapper">
                <div class="img-preloader-full">
                    <div class="img-preloader-progress"></div>
                </div>
            </div>
            <img draggable="false" class="main-image" src="${url}" draggable="false">
        </div>`);

        make_clickable(item.querySelector('.attachment-remove'), (e) => remove_attachment(file_id_hex));

        divs['msg-attachments-row'].appendChild(item);
        divs['msg-attachments-row'].classList.remove('dhide');
    } else {
        const item = HTML(`
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

        make_clickable(item.querySelector('.attachment-remove'), (e) => remove_attachment(file_id_hex));

        divs['msg-attachments-files-row'].appendChild(item);
        divs['msg-attachments-files-row'].classList.remove('dhide');
    }

    if (CONFIG_DEBUG_PRINT) {
        console.debug('Started upload of file', file_id_hex);
    }
}

function wait_for_image_preview(img, file_id_hex, level, interval=5000, preview_full=false) {
    let img_try = 0;
    if (img) {
        img.src = preview_full ? `/storage/${file_id_hex}` : `/storage/${file_id_hex}-${level}`;
        const interval_id = setInterval(async () => {
            const resp = await fetch(`/storage/${file_id_hex}-${level}`);
            //console.log('test result', resp.status);
            if (resp.status == 200) {
                img.src = `/storage/${file_id_hex}-${level}`;
                clearInterval(interval_id);
            } else if (img_try == CONFIG_IMAGE_TRIES) {
                // we failed
                clearInterval(interval_id);
            }
            img_try++;
        }, interval);
    }
}

function l_upload_complete(file) {
    const bigint = BigInt(file.id);
    const file_id_hex = bigint.toString(16);

    if (!cancelled_files.includes(file_id_hex)) {
        document.querySelector(`.any-attachment[data-file-id="f-${file_id_hex}"] .preloader-wrapper`).classList.add('dhide');
        
        const attach = { 'id': file_id_hex, 'ext': file.extension };

        if (is_supported_image(file.extension)) {
            const img = document.querySelector(`.attachment[data-file-id="f-${file_id_hex}"] img.main-image`);
            attach.width = file.width;
            attach.height = file.height;
            wait_for_image_preview(img, file_id_hex, 2, 5000, true);
        } else {
            attach.name = file.name;
            document.querySelector(`.file-attachment[data-file-id="f-${file_id_hex}"] .upload-done`).classList.remove('dhide');
        }
        
        attaching_files.push(attach);
    }

    --active_uploads;
    refresh_message_send();
}

function l_upload_progress(file_id, loaded, total) {
    const file_id_hex = BigInt(file_id).toString(16);
    const percent = Math.ceil(loaded / total * 100);
    // console.log(percent)
    document.querySelector(`.any-attachment[data-file-id="f-${file_id_hex}"] .img-preloader-progress`).style.width = percent + '%';
}

function l_upload_error(err) {
    // TODO: this might get called before on_start, and break the active uploads balance!
    --active_uploads;
    refresh_message_send();
    console.error('uploader:', err);
}

function l_file_dragenter(e) {
    e.preventDefault();
    divs['message-input-drag-indicator'].classList.remove('dhide');
}

function l_file_dragover(e) {
    e.preventDefault();
    divs['message-input-drag-indicator'].classList.remove('dhide');
}

function l_file_dragleave(e) {
    e.preventDefault();
    divs['message-input-drag-indicator'].classList.add('dhide');
}

async function l_file_drop(e, uploader) {
    e.preventDefault();

    divs['message-input-drag-indicator'].classList.add('dhide');
    // TODO: do not create preview if not an image
    if (e.dataTransfer.items) {
        const items = [...e.dataTransfer.items];
        for (const item of items) {
              if (item.kind === 'file') {
                const file = item.getAsFile();
                uploader_manual_submit(uploader, file);
            }
        }
    } else {
        console.error('Your browser does not support DataTransferItemList, and support for DataTransfer is not yet implemented, sorry!');
    }
}

function l_show_attachments() {
    const current_channel_id = ls_get_current_channel_id();
    const attachment_count = count_attachments(current_channel_id);
    popup_show(`Attachments (${attachment_count})`, channel_attachments_html(current_channel_id), []);
}

function l_password_input_changed() {
    const p1 = divs['change-password-input'].value;
    const p2 = divs['confirm-password-input'].value;

    if (p1.length > 0 && p1 === p2) {
        divs['confirm-password-change'].classList.remove('tdisabled');
    } else {
        divs['confirm-password-change'].classList.add('tdisabled');
    }
}

function l_change_password() {
    const p1 = divs['change-password-input'].value;
    
    websocket_send_change_password(p1);

    divs['change-password-input'].value = '';
    divs['confirm-password-input'].value = '';
    divs['confirm-password-change'].classList.add('tdisabled');
}

function l_message_paste(e, uploader) {
    const items = (e.clipboardData || e.originalEvent.clipboardData).items;
    for (const item of items) {
        if (item.kind === 'file') {
            const file = item.getAsFile();
            uploader_manual_submit(uploader, file);
        }
    }
}

function l_toggle_sidebar() {
    divs['sidebar'].classList.toggle('shown');
    if (touch_device && divs['sidebar'].classList.contains('shown')) {
        channel_scroller.context.channel_reset_timeout_id = setTimeout(() => switch_to_channel(null), 500);
    }
}

function l_show_channel_settings(uploader) {
    const current_channel_id = ls_get_current_channel_id();
    const channel = ls_get_channel(current_channel_id);

    if (channel) {
        popup_show(
            channel.title,
            'Very interesting information about this channel',
            [{
                'class': 'primary',
                'text': 'Change chanel name',
                'action': () => {
                    popup_show("Edit channel name", channel_change_name_html(channel),
                               [{ 'text': 'Cancel' },
                                   {
                                       'text': 'Submit', 'class': 'primary',
                                       'action': () => {
                                           let title_field = divs['popup'].querySelector("#change-channel-title-input")
                                           let new_title = title_field.value.trim();
                                           if (new_title !== channel.title)
                                               enqueue_change_title(channel.id, new_title);
                                       }
                                   }]);
                    return false;
                }
            }, {
                'class': 'primary',
                'text': 'Change avatar',
                'action': () => {
                    document.querySelector('#channel-avatar-upload-hidden-button').click();
                    return false;
                }
            }]
        )
    }
}

function l_channel_avatar_upload_started(file, channel_id) {
    console.log(channel_id);
    const url = URL.createObjectURL(file);
    document.querySelector('#selected-channel-avatar img').src = url;
    document.querySelector(`.channel[data-channel-id="${channel_id}"] .avatar img`).src = url;
}

function l_channel_avatar_upload_complete(file, channel_id) {
    // TODO: this might be converting back and forth
    const bigint = BigInt(file.id);
    const file_id_hex = bigint.toString(16);
    websocket_send_set_channel_avatar(channel_id, file.id);
}

function set_message_input(element, text) {
    element.value = text;
    element.selectionStart = element.selectionEnd = 0;
    l_message_input(element);
}

function clear_message_input(input_element) {
    input_element.value = '';
    input_element.selectionStart = input_element.selectionEnd = 0;
    l_message_input(input_element);
}

function l_message_input(input_element) {
    input_element.style.height = '0px';
    input_element.scrollHeight; // force reflow
    input_element.style.height = input_element.scrollHeight + 'px';
}

function l_toggle_darkmode(e) {
    const checked = e.target.checked;

    if (checked) {
        divs['main-page'].classList.add('dark');
    } else {
        divs['main-page'].classList.remove('dark');
    }

    ls_set_darkmode(checked);
}

function l_global_hotkey(e) {
    if (e.code === 'Slash') {
        e.preventDefault();
        const current_channel_id = ls_get_current_channel_id();
        if (current_channel_id !== null) {
            divs['message-input'].focus();
        }
    }
}

function l_todo() {
    popup_show(
        'Congratulations!', 
        'You have found a button which has not yet been implemented',
        [{'class': 'primary', 'text': 'ok'}, {'text': 'cancel'}]
    );
}

function find_and_bind(silence = false, mobile = false) {

    const uploader = uploader_create(document.getElementById('file-upload-hidden-button'), {
        'url_create': '/upload/upload-req',
        'url_status': '/upload/upload-status',
        'url_upload': '/upload/upload-do',
        'on_start':    l_upload_started,
        'on_complete': l_upload_complete,
        'on_progress': l_upload_progress,
        'on_error':    l_upload_error,
    });

    const autocomplete_onkeydown = autocomplete_set_on_element(find('message-input'), get_message_autocomplete);
    
    const items = [
        { 'key': 'main-page' },
        { 'key': 'container' },
        { 'key': 'selected-channel-title' },
        { 'key': 'channel-title', 'mobile_only': true },
        { 'key': 'toggle-emoji-input' },
        { 'key': 'emoji-list' },
        { 'key': 'typing-indicator' },
        { 'key': 'reply-preview-author' },
        { 'key': 'reply-preview-text' },
        { 'key': 'message-input-additions' },
        { 'key': 'popup' },
        { 'key': 'context-menu' },
        { 'key': 'user-autocomplete-variants' },
        { 'key': 'popup-dimmer' },
        { 'key': 'popup-container' },
        { 'key': 'file-upload-hidden-button' },
        { 'key': 'msg-attachments-row' },
        { 'key': 'msg-attachments-files-row' },
        { 'key': 'message-input-drag-indicator' },
        { 'key': 'channel-list' },
        { 'key': 'sidebar' },
        { 'key': 'channel-avatar', 'mobile_only': true },
        { 'key': 'channels-spinner' },

        { 'key': 'channel-content',         'type': 'dragover',  'func': l_file_dragover },
        { 'key': 'message-input-drag-indicator',         'type': 'dragleave', 'func': l_file_dragleave },
        { 'key': 'message-input-drag-indicator',         'type': 'drop',      'func': (e) => l_file_drop(e, uploader) },
        { 'key': 'channel-content',         'type': 'click',     'func': () => { if (touch_device) context_menu_hide() } },
        { 'key': 'message-send-button',     'type': 'click',     'func': try_send },
        { 'key': 'search-input',            'type': 'keydown',   'func': search_keydown },
        
        { 'key': 'message-input',           'type': 'keydown',   'func': (e) => l_message_key(e, autocomplete_onkeydown) },
        // { 'key': 'message-input',           'type': 'keyup',     'func': l_message_key }, // TODO: keyup for what?
        { 'key': 'message-input',           'type': 'paste',     'func': (e) => l_message_paste(e, uploader) },
        { 'key': 'message-input',           'type': 'input',     'func': () => l_message_input(divs['message-input']) },
        { 'key': 'message-input',           'type': 'change',    'func': () => l_message_input(divs['message-input']) },

        { 'key': 'message-input',           'type': 'input',     'func': l_handle_typing },
        { 'key': 'cancel-decoration',       'type': 'click',     'func': l_cancel_message },
        { 'key': 'toggle-search',           'type': 'click',     'func': l_toggle_search },
        { 'key': 'pin-preview',             'type': 'click',     'func': l_pin_jump },
        { 'key': 'pin-unpin',               'type': 'click',     'func': l_pin_unpin },
        { 'key': 'toggle-people',           'type': 'click',     'func': l_show_channel_info },
        { 'key': 'toggle-attachments',      'type': 'click',     'func': l_show_attachments },
        { 'key': 'toggle-settings',         'type': 'click',     'func': l_show_settings },
        { 'key': 'toggle-create-channel',   'type': 'click',     'func': l_create_channel },
        { 'key': 'find-chatter',            'type': 'input',     'func': l_redraw_user_autocomplete },
        { 'key': 'b-chat',                  'type': 'click',     'func': l_show_direct_creation_ui },
        { 'key': 'b-channel',               'type': 'click',     'func': l_show_channel_creation_dialog },
        { 'key': 'channel-create-confirm',  'type': 'click',     'func': l_confirm_channel_creation },
        { 'key': 'chat-create-confirm',     'type': 'click',     'func': l_confirm_direct_creation },
        { 'key': 'upload-file-button',      'type': 'click',     'func': l_open_file_picker_dialog },
        { 'key': 'toggle-sidebar',          'type': 'click',     'func': l_toggle_sidebar },
        { 'key': 'toggle-sidebar2',         'type': 'click',     'func': l_toggle_sidebar },
        { 'key': 'toggle-sidebar3',         'type': 'click',     'func': l_toggle_sidebar },
        { 'key': 'selected-channel-avatar', 'type': 'click',     'func': () => l_show_channel_settings(uploader) },

        { 'key': 'change-password-input',   'type': 'input',     'func': l_password_input_changed },
        { 'key': 'confirm-password-input',  'type': 'input',     'func': l_password_input_changed },
        { 'key': 'confirm-password-change', 'type': 'click',     'func': l_change_password },

        { 'key': 'settings-logout',         'type': 'click',     'func': l_logout },

        { 'key': 'container',               'type': 'keydown',   'func': l_global_hotkey },
        { 'key': 'dark-mode-toggle',        'type': 'change',    'func': l_toggle_darkmode },
    ];

    for (const item of items) {
        const element = find(item.key);

        if (element === null) {
            if (!silence && (mobile || !item.mobile_only)) {
                console.error(`Could not find DOM element '${item.key}'`);
            }
            continue;
        }

        divs[item.key] = element;

        if ('type' in item && 'func' in item) {
            if (item.type === 'click') {
                make_clickable(element, item.func);
            } else {
                element.addEventListener(item.type, item.func);
            }
        }
    }

    if (touch_device) {
        divs['main-page'].classList.add('touch');
    }

    const channel_uploader = uploader_create(document.getElementById('channel-avatar-upload-hidden-button'), {
        'url_create': '/upload/upload-req',
        'url_status': '/upload/upload-status',
        'url_upload': '/upload/upload-do',
        'data_func':   () => ls_get_current_channel_id(),
        'on_start':    l_channel_avatar_upload_started,
        'on_complete': l_channel_avatar_upload_complete,
    });
}

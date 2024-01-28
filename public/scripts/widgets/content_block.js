/**
 *
 * @param {Object} options
 * @param {HTMLElement} options.scroller_root
 * @param {HTMLElement} options.message_input_root
 * @param {HTMLElement} options.block_root
 * @param {HTMLElement} options.uploader_btn
 * @param {HTMLElement} options.drag_indicator
 * @constructor
 */
function ContentBlock(options) {
    let message_input = null;
    let scroller = null;

    let channel_id = null;
    let thread_id = null;

    let replying_index = null;
    let editing_index = null;

    let uploader = null;
    let uploader_btn = null;
    let active_uploads = null;
    let attaching_files = [];
    let cancelled_files = [];

    let block_root = null;
    let drag_indicator = null;

    function init() {
        uploader_init();
        message_input = new MessageInput({
                                             root: options.message_input_root,
                                             click_send: send_message,
                                             click_upload: () => uploader_btn.click(),
                                             cansel_message: cancel_message,
                                             start_edit_last_mine: start_edit_last_mine,
                                             handle_past: (e) => l_message_paste(e, uploader)
                                         });
        message_input.init();

        scroller = new Scroller({root: options.scroller_root, handle_reply: initiate_reply, handle_edit: initiate_edit, in_thread: true});
        scroller.init();
        this.scroller = scroller;
        this.is_focused = message_input.is_focused;

        block_root = options.block_root;
        drag_indicator = options.drag_indicator;
        drag_events_init();

        block_root.addEventListener('click', () => {
            if (touch_device) context_menu_hide()
        })
    }

    function uploader_init() {
        uploader_btn = options.uploader_btn;
        uploader = uploader_create(uploader_btn, {
            'url_create': '/upload/upload-req',
            'url_status': '/upload/upload-status',
            'url_upload': '/upload/upload-do',
            'on_start': upload_started,
            'on_complete': upload_complete,
            'on_progress': upload_progress,
            'on_error': upload_error,
        });
    }

    function upload_started(file) {
        const file_id_hex = BigInt(file.id).toString(16);

        ++active_uploads;
        refresh_message_send();

        message_input.add_attachment(file, file_id_hex, remove_attachment);

        if (CONFIG_DEBUG_PRINT) {
            console.debug('Started upload of file', file_id_hex);
        }
    }

    function remove_attachment(file_id_hex) {
        cancelled_files.push(file_id_hex);

        for (let i = 0; i < attaching_files.length; ++i) {
            if (attaching_files[i].id === file_id_hex) {
                attaching_files.splice(i, 1);
                break;
            }
        }

        message_input.remove_attachment(file_id_hex);
    }

    function upload_complete(file) {
        const file_id_hex = BigInt(file.id).toString(16);

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

    function upload_progress(file_id, loaded, total) {
        const file_id_hex = BigInt(file_id).toString(16);
        const percent = Math.ceil(loaded / total * 100);
        document.querySelector(`.any-attachment[data-file-id="f-${file_id_hex}"] .img-preloader-progress`).style.width = percent + '%';
    }

    function upload_error(err) {
        // TODO: this might get called before on_start, and break the active uploads balance!
        --active_uploads;
        refresh_message_send();
        console.error('uploader:', err);
    }

    function drag_events_init() {
        drag_indicator.addEventListener('dragleave', e => {
            e.preventDefault();
            drag_indicator.classList.add('dhide');
        });
        block_root.addEventListener('dragover', e => {
            e.preventDefault();
            drag_indicator.classList.remove('dhide');
        });
        drag_indicator.addEventListener('drop', file_drop);
    }

    function file_drop(e) {
        e.preventDefault();
        drag_indicator.classList.add('dhide');
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

    function send_message(msg) {
        if (active_uploads > 0) {
            return false;
        }
        if (msg.length > 0 || attaching_files.length > 0) {

            if (editing_index !== null) {
                enqueue_edit(channel_id, editing_index, msg, true);
            } else if (replying_index !== null) {
                enqueue_message(channel_id, thread_id, msg, true);
                enqueue_reply(channel_id, replying_index, true);
            }

            if (attaching_files.length > 0) {
                if (replying_index === null && editing_index === null) {
                    // NOTE(aolo2): we allow just attaches as replies, and also attaching to old messages.
                    // This means that we should create an empty message for the attach only if we are not
                    // replying or editing
                    enqueue_message(channel_id, thread_id, msg, true);
                }

                let attach_index = -1;

                if (editing_index !== null) {
                    attach_index = editing_index;
                }

                for (const file of attaching_files) {
                    enqueue_attach(channel_id, attach_index, file, true);
                }
            } else if (replying_index === null && editing_index === null) {
                add_tmp_message(msg);
                enqueue_message(channel_id, thread_id, msg, true);
                scroller.turn_on_follow_mode();
            }
            cancel_message();
        }
        push_unsent(channel_id);
        return true;
    }

    function add_tmp_message(msg) {
        const me = ls_get_me();
        const tmp_message = {
            'type': RECORD.MESSAGE,
            'channel_id': channel_id,
            'author_id': me.user_id,
            'timestamp': Date.now() / 1000,
            'text': msg,
            'reply_to': -1,
            'thread_id': thread_id ? thread_id : -1
        };

        if (!(channel_id in unconfirmed_messages)) {
            unconfirmed_messages[channel_id] = 0;
        }

        if (!(channel_id in confirmed_messages)) {
            confirmed_messages[channel_id] = 0;
        }

        unconfirmed_messages[channel_id]++;
        scroller.add_tmp(tmp_message, confirmed_messages[channel_id] + unconfirmed_messages[channel_id]);
    }

    function set_channel_id(c_id) {
        channel_id = c_id;
        scroller.set_channel(c_id);
    }

    function set_thread_id(t_id) {
        thread_id = t_id;
        scroller.set_filter(msg => msg.thread_id === thread_id);
    }

    function initiate_reply(index, message) {
        replying_index = index;
        message_input.set_additions(message);
    }
    function cancel_reply() {
        replying_index = null;
        message_input.clear_additions();
    }
    function initiate_edit(index, message) {
        editing_index = index;
        message_input.set_input_text(message.text);
        message_input.set_additions(message);
    }
    function cancel_edit() {
        editing_index = null;
        message_input.clear_additions();
    }

    function cancel_attachment() {
        attaching_files = [];
        cancelled_files = [];
        message_input.clear_attachments();
    }
    function cancel_message() {
        if (editing_index !== null) cancel_edit();
        if (replying_index !== null) cancel_reply();
        cancel_attachment();
    }

    function start_edit_last_mine() {
        cancel_message();
        const last_mine = scroller.get_last_mine_message();
        if (last_mine.i !== -1) {
            initiate_edit(last_mine.i, last_mine.message);
        }
    }

    function refresh_message_send() {
        if (active_uploads > 0) {
            message_input.disable_send_btn();
        } else {
            message_input.enable_send_btn();
        }
    }


    this.init = init;
    this.set_thread = set_thread_id;
    this.set_channel = set_channel_id;
    this.handle_msg_recv = null; // TODO: create a function to unload (?) messages_recv
}

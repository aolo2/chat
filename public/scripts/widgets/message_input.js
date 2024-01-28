function get_focused_input(element) {
    let inputs = element.querySelectorAll('.message-input-textarea');
    inputs = inputs.filter(e => e.activeElement);
    return inputs.length > 0 ? inputs[0] : null;
}


/**
 * Message input widget
 * @param {Object} options
 * @param {HTMLElement} options.root
 * @param {(string) => boolean} options.click_send
 * @param {function()} options.click_upload
 * @param {function()} options.cansel_message
 * @param {function(int)} options.start_edit_last_mine
 * @param {function(Event)} options.handle_past
 * @constructor
 */
function MessageInput(options) {
    let html_elements = null;
    let click_send = null;
    let click_upload = null;
    let cansel_message = null;
    let autocomplete_onkeydown = null;


    function init() {
        html_elements = {};
        html_elements['root'] = options['root'];
        click_send = options['click_send'];
        click_upload = options['click_upload'];
        cansel_message = options['cansel_message'];

        render();
    }


    function render() {
        html_elements['root'].innerHTML = get_inner_html();
        html_elements['message-send-button'] = html_elements.root.querySelector('.message-send-button');
        html_elements['message-input'] = html_elements.root.querySelector('.message-input-textarea');
        html_elements['upload-file-button'] = html_elements.root.querySelector('.upload-file-button');
        html_elements['toggle-emoji-input'] = html_elements.root.querySelector('.toggle-emoji-input');

        autocomplete_onkeydown = autocomplete_set_on_element(html_elements['message-input'], get_message_autocomplete);
        html_elements['upload-file-button'].addEventListener('click', click_upload);
        html_elements['message-send-button'].addEventListener('click', handle_send_btn_click);
        html_elements['message-input'].addEventListener('keydown', (e) => handle_message_keydown(e, autocomplete_onkeydown));
        html_elements['message-input'].addEventListener('paste', options['handle_paste']);
        html_elements['message-input'].addEventListener('input', (e) => l_message_input(html_elements['message-input']) );
        html_elements['message-input'].addEventListener('change', (e) => l_message_input(html_elements['message-input']) );
        add_toggle_emoji(html_elements['toggle-emoji-input'], html_elements['message-input']);


        html_elements['reply-preview-author'] = html_elements.root.querySelector('.reply-preview-author');
        html_elements['reply-preview-text'] = html_elements.root.querySelector('.reply-preview-text');
        html_elements['message-input-additions'] = html_elements.root.querySelector('.message-input-additions');
        html_elements['close-additions'] = html_elements.root.querySelector('.cancel-decoration');

        html_elements['close-additions'].addEventListener('click', cansel_message);

        html_elements['msg-attachments-row'] = html_elements.root.querySelector('.msg-attachments-row');
        html_elements['msg-attachments-files-row'] = html_elements.root.querySelector('.msg-attachments-files-row');

    }

    function get_inner_html() {
        return `
        <div class="row3 dhide msg-attachments-row">
        </div>
        <div class="row4 dhide msg-attachments-files-row">
        </div>
        <div class="row1 dhide message-input-additions">
            <div class="reply-preview">
                <div class="reply-preview-author">
                </div>
                <div class="reply-preview-text">
                </div>
            </div>
            <div class="action light cancel-decoration">
                <img draggable="false" src="static/icons/cross.svg">
            </div>
        </div>
        <div class="row2">
            <div class="action upload-file-button">
                <img draggable="false" src="static/icons/perfect_attach.svg">
            </div>
            <textarea class="message-input-textarea"></textarea>
            <div class="light action input-embed toggle-emoji-input">
                <img draggable="false" src="static/icons/react2.svg">
            </div>
            <div class="action light message-send-button">
                <img draggable="false" src="static/icons/send.svg">
            </div>
            <div class="typing-indicator">
                <!-- a.olokhtonov is typing... -->
            </div>
        </div>`
    }

    function handle_message_keydown(e, autocomplete_onkeydown) {
        const cancel = autocomplete_onkeydown(e);
        if (cancel) {
            return;
        }

        if (e.code === 'Enter' && !e.shiftKey) {
            handle_send_btn_click(e);
        } else if (e.code === 'Escape') {
            cansel_message();
        } else if (e.code === 'ArrowUp' && divs['message-input'].value.length === 0) {
            options['start_edit_last_mine']();
        }
    }

    function handle_send_btn_click(e) {
        e.preventDefault();
        const msg = html_elements['message-input'].value.trim();

        let send_done = click_send(msg);
        if (send_done) {
            // TODO(mk): clear reply preview, edit preview etc
            clear_message_input(html_elements['message-input']);
        }
    }

    function set_additions(message) {
        html_elements['reply-preview-author'].innerHTML = ls_find_user(message.author_id).name;
        html_elements['reply-preview-text'].innerHTML = process_message_text(message.text).text;
        html_elements['message-input-additions'].classList.remove('dhide');
        if (!touch_device) html_elements['message-input'].focus();
        l_message_input(html_elements['message-input']);
    }

    function clear_additions() {
        clear_message_input(html_elements['message-input']);
        html_elements['message-input-additions'].classList.add('dhide');
    }

    function set_input_text(text) {
        set_message_input(html_elements['message-input'], text);
    }

    function disable_send_btn() {
        html_elements['message-send-button'].classList.add('tdisabled');
    }

    function enable_send_btn() {
        html_elements['message-send-button'].classList.remove('tdisabled');
    }

    function add_attachment(file, file_id_hex, remove_callback) {
        let item = get_attachment_preview_item(file, file_id_hex, remove_callback);

        if (is_supported_image(uploader_server_ext(file.type))) {
            html_elements['msg-attachments-row'].appendChild(item);
            html_elements['msg-attachments-row'].classList.remove('dhide');
        } else {
            html_elements['msg-attachments-files-row'].appendChild(item);
            html_elements['msg-attachments-files-row'].classList.remove('dhide');
        }
    }

    function remove_attachment(file_id_hex) {
        document.querySelector(`.any-attachment[data-file-id="f-${file_id_hex}"]`).remove();

        if (html_elements.root.querySelector('.msg-attachments-row .any-attachment') === null) {
            html_elements['msg-attachments-row'].classList.add('dhide');
        }

        if (html_elements.root.querySelector('.msg-attachments-files-row .any-attachment') === null) {
            html_elements['msg-attachments-files-row'].classList.add('dhide');
        }
    }

    function clear_attachments() {
        html_elements['msg-attachments-row'].innerHTML = '';
        html_elements['msg-attachments-files-row'].innerHTML = '';

        html_elements['msg-attachments-row'].classList.add('dhide');
        html_elements['msg-attachments-files-row'].classList.add('dhide');
    }

    this.init = init;
    this.set_additions = set_additions;
    this.clear_additions = clear_additions;
    this.clear_attachments = clear_attachments;
    this.set_input_text = set_input_text;
    this.enable_send_btn = enable_send_btn;
    this.disable_send_btn = disable_send_btn;
    this.add_attachment = add_attachment;
    this.remove_attachment = remove_attachment;
}

const touch_device = (('ontouchstart' in window) || (navigator.maxTouchPoints > 0) || (navigator.msMaxTouchPoints > 0));

let login_form = null;
let login_field = null;
let submit_button = null;
let password_field = null;
let failed_message = null;

let online = false;

function submit_login(e) {
    e.preventDefault();

    const login = login_field.value.trim();
    const password = password_field.value.trim();

    if (login.length === 0 || password.length === 0) {
        return;
    }

    if (online) {
        websocket_send_auth(login, password);
    }
}

function reenable_submit() {
    const login = login_field.value.trim();
    const password = password_field.value.trim();

    if (login.length === 0 || password.length === 0) {
        submit_button.classList.add('tdisabled');
        return false;
    } else {
        submit_button.classList.remove('tdisabled');
        return true;
    }
}

async function auth_success(buffer, dataview) {
    const session_id = dataview.getBigUint64(1, true);
    const user_id = dataview.getUint32(9, true);

    const me = {
        'session_id': session_id.toString(),
        'user_id': user_id,
    };

    const old_me = ls_get_me();

    if (old_me && old_me.user_id) {
        if (old_me.user_id !== user_id) {
            // NOTE(aolo2): if we are already logged in as another user - clear everything. We don't
            // want data from different sessions to get mixed up
            ls_clear();
            await idb_clear();
            ls_set_me(me);
            window.location.href = '/';
        }
    } else {
        // NOTE(aolo2): if we have already logged in as this user, then don't to it again
        ls_set_me(me);
    }

    window.location.href = '/';
}

function auth_fail() {
    failed_message.classList.remove('dhide');
}

function find_divs() {
    login_form = find('login-form');
    login_field = find('login-form-login');
    password_field = find('login-form-password');
    submit_button = find('login-form-submit');
    failed_message = find('login-form-fail');
}

function bind_listeners() {
    login_field.addEventListener('input', reenable_submit);
    password_field.addEventListener('input', reenable_submit);

    const i = setInterval(() => {
        const enabled = reenable_submit();
        if (enabled) {
            clearInterval(i);
        }
    }, 500);

    make_clickable(submit_button, submit_login);
    
    window.addEventListener('keydown', (e) => {
        if (e.code === 'Slash') {
            login_field.focus();
            e.preventDefault();
        }
    })
}

function set_style() {
    if (window.screen.height > window.screen.width) {
        document.body.classList.add('vertical');
    }
}

async function main() {
    if (localStorage.getItem('bc-session-id')) {
        window.location.href = '/';
    }

    set_style();
    find_divs();
    bind_listeners();
    ls_init();

    await idb_init();

    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.AUTH_SUCCESS, auth_success);
    websocket_register_handler(WS_MESSAGE_TYPE.SERVER.AUTH_FAIL, auth_fail);

    websocket_connect(CONFIG_WS_URL, () => {
        online = true;
    });
}

document.addEventListener('DOMContentLoaded', () => {
    document.fonts.ready.then(main);
}, false);

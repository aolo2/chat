let sent_sound = null;
let sent_ready = false;

let notification_sound = null;
let notification_ready = false;
let notification_timer = null;
let notification_disabled = false;
let notification_serviceworker_registration = null;

let blinking_title_interval = null;
let blinking_title_cancel_timeout = null;

function sent_play() {
    if (sent_ready) {
        sent_sound.play();
    }
}

function notification_toggle_sound(e) {
    notification_disabled = !e.checked;
}

function notification_init() {
    notification_sound = new Audio('/static/sounds/notification.wav');
    notification_sound.addEventListener('canplaythrough', (e) => {
        notification_ready = true;
    });

    navigator.serviceWorker.register('/static/scripts/service-worker.js', { scope: '/' }).then((r) => {
        notification_serviceworker_registration = r;
    }, (err) => {
        console.error(err);
    });

    // sent_sound = new Audio('/static/sounds/select.wav');
    // sent_sound.addEventListener('canplaythrough', (e) => {
    //     sent_ready = true;
    // });

    document.addEventListener("visibilitychange", () => {
        if (!document.hidden) {
            clearTimeout(blinking_title_cancel_timeout);
            clearInterval(blinking_title_interval);
            document.getElementById('favicon').href = '/static/icons/bullet.svg';
            document.title = 'Bullet.Chat';
        }
    });
}

function notification_push(record) {
    if ('Notification' in window && window.Notification.permission === 'granted') {
        if (notification_serviceworker_registration) {
            if (record) {
                const author = ls_find_user(record.author_id);
                if (author) {
                    notification_serviceworker_registration.showNotification(
                        author.name,
                        {
                            'body': record.text,
                            'timestamp': record.timestamp,
                        },

                    );
                }
            }
        }
    }
}

function notification_all_read() {
    document.querySelectorAll('.toggle-sidebar').forEach(i => i.classList.remove('unread'));
}

function notification_new_unread(silent = false) {
    document.querySelectorAll('.toggle-sidebar').forEach(i => i.classList.add('unread'));

    if (!document.hidden) {
        return;
    }
    
    document.getElementById('favicon').href = '/static/icons/bullet_mark.svg';

    if (silent) {
        return;
    }

    if (notification_ready && !notification_disabled) {
        if (notification_timer !== null) {
            clearTimeout(notification_timer);
        }

        notification_timer = setTimeout(() => {
            notification_sound.play();
            notification_timer = null;
        }, 500);
    }

    notification_blink_tab_from_some_time();
}

function notification_blink_tab_from_some_time(unread_count) {
    const old_title = 'Bullet.Chat';
    let blink_step = 0;

    let unread_string = 'New messages';

    clearTimeout(blinking_title_cancel_timeout);
    clearInterval(blinking_title_interval);

    blinking_title_interval = setInterval(() => {
        if (blink_step === 0) {
            document.title = unread_string;
            blink_step = 1;
        } else if (blink_step === 1) {
            document.title = old_title;
            blink_step = 0;
        }
    }, 800);

    blinking_title_cancel_timeout = setTimeout(() => {
        clearInterval(blinking_title_interval);
        document.title = old_title;
    }, 5000);
}

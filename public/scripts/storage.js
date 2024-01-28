/* 
Store and update data in localStorage and indexedDB

indexedDB: processed records
localStorage: everything else (messages aren't here since they can exceed the localStorage size limit)

Records get their decorations applied on recv, so "messages" dict in js and the indexedDB data are already
edited, flagged as deleted etc.
*/

/********************************************/
/**** Blazing(TM) fast in-memory storage ****/
/********************************************/
let editing_index = null;
let replying_index = null;
let reacting_index = null;

let channel_creation_state = 0;
let active_uploads = 0;

const invite_selected = [];
let attaching_files = [];
let cancelled_files = [];

let confirmed_messages = {};
let unconfirmed_messages = {};

// to show the spinner until this becomes 0 again
let n_requested_channels = 0;

// Map of messages per channel. ALL RECORDS are saved per channel,
// but also APPLIED. I.e. if there's a message edit, the EDIT record
// is saved, but the ORIGINAL message text is also edited in-place.
// Same for replies, reactions etc.
let messages = {}; // This might get HUGE, keep an eye

function get_block_data(blocks, block_id) {
    for (const block of blocks) {
        if (block.id === block_id) {
            return block.data;
        }
    }

    return null;
}

function user_in_channel(channel, user_id) {
    if (!channel) {
        return false;
    }

    for (const user of channel.users) {
        if (user.id === user_id) {
            return true;
        }
    }

    return false;
}

function get_channel_title(channel) {
    let title = channel.title;    

    if (channel.is_dm) {
        const them = get_direct_partner(channel);
        title = them ? them.name : 'Secret';
    }

    return '<span class="channel-title">' + title + '</span>';
}

/*************************/
/****** LocalStorage *****/
/*************************/
let lsDB = null;
const lsCHANNEL_KEY = 'bc-channel';
const lsUSER_KEY = 'bc-user';
const lsCHANNEL_LIST_KEY = 'bc-channel-list';
const lsCURRENT_CHANNEL_KEY = 'bc-current-channel';
const lsFIRST_LOAD_KEY = 'bc-first-load';
const lsUSER_LIST_KEY = 'bc-user-list'; // TODO @speed this might need to become a hash-map in the future
const lsBLOB_KEY = 'bc-blob';
const lsDARKMODE_KEY = 'bc-darkmode';

function _ls_getjson(key) {
    const text = lsDB.getItem(key);
    
    try {
        const obj = JSON.parse(text);
        return(obj);
    } catch (e) {}

    return null;
}

function _ls_setjson(key, val) {
    try {
        const text = JSON.stringify(val);
        lsDB.setItem(key, text);
        return true;
    } catch (e) {}

    console.error('Could not serialize object to write to localStorage:' + val);

    return false;
}

function ls_get_channel_list() {
    return _ls_getjson(lsCHANNEL_LIST_KEY);
}

function ls_set_channel_list(channel_list) {
    return _ls_setjson(lsCHANNEL_LIST_KEY, channel_list);
}

function ls_join_channel(channel_id) {
    const list = _ls_getjson(lsCHANNEL_LIST_KEY);
    if (!list.includes(channel_id)) {
        list.push(channel_id);
        return _ls_setjson(lsCHANNEL_LIST_KEY, list);
    }
    return false;
}

function ls_leave_channel(channel_id) {
    const list = _ls_getjson(lsCHANNEL_LIST_KEY);
    const at = list.indexOf(channel_id);
    if (at !== -1) {
        list.splice(at, 1);
        return _ls_setjson(lsCHANNEL_LIST_KEY, list);
    }
    return false;
}

function ls_find_user(id) {
    const users = _ls_getjson(lsUSER_LIST_KEY);

    if (!users) return null;

    for (const user of users) {
        if (user.id === id) {
            return user;
        }
    }

    return null;
}

function ls_find_user_by_login(login) {
    const users = _ls_getjson(lsUSER_LIST_KEY);

    if (!users) return null;

    for (const user of users) {
        if (user.login === login) {
            return user;
        }
    }

    return null;   
}

function ls_set_user(updated_user) {
    const users = _ls_getjson(lsUSER_LIST_KEY);

    for (let i = 0; i < users.length; ++i) {
        const user = users[i];
        if (user.id === updated_user.id) {
            users[i] = updated_user;
            _ls_setjson(lsUSER_LIST_KEY, users);
            break;
        }
    }
}

function ls_set_users(users) {
    return _ls_setjson(lsUSER_LIST_KEY, users);
}

function ls_set_current_channel(channel_id) {
    return _ls_setjson(lsCURRENT_CHANNEL_KEY, channel_id);
}

function ls_get_current_channel_id() { return _ls_getjson(lsCURRENT_CHANNEL_KEY); }
function ls_get_channel(channel_id) { return _ls_getjson(lsCHANNEL_KEY + '-' + channel_id); }
function ls_set_channel(channel) { return _ls_setjson(lsCHANNEL_KEY + '-' + channel.id, channel); }
function ls_get_me() { return _ls_getjson(lsUSER_KEY); }
function ls_set_me(user) { return _ls_setjson(lsUSER_KEY, user); }
function ls_get_users() { return _ls_getjson(lsUSER_LIST_KEY); }
function ls_get_first_load() { return _ls_getjson(lsFIRST_LOAD_KEY); }
function ls_set_first_load(value) { return _ls_setjson(lsFIRST_LOAD_KEY, value); }
function ls_get_darkmode() { return _ls_getjson(lsDARKMODE_KEY); }
function ls_set_darkmode(value) { return _ls_setjson(lsDARKMODE_KEY, value); }

function ls_save_settings(key, value) {
    const full_key = lsBLOB_KEY + '-' + key;
    return _ls_setjson(full_key, value);
}

function ls_init() {
    lsDB = window.localStorage;
    
    if (!lsDB.getItem(lsCHANNEL_LIST_KEY)) {
        lsDB.setItem(lsCHANNEL_LIST_KEY, '[]');
    }

    if (!lsDB.getItem(lsFIRST_LOAD_KEY)) {
        lsDB.setItem(lsFIRST_LOAD_KEY, true);
    }
}

function ls_clear() {
    lsDB.clear();   
}

/*************************/
/******* IndexedDB *******/
/*************************/
let iDB = null;
const DATABASE = 'bc-indexeddb';
const STORE = 'bc-store';

const idb_init = asyncify(_idb_init_cb);
const idb_save = asyncify(_idb_save_cb);
const idb_restore = asyncify(_idb_restore_cb);
const idb_clear = asyncify(_idb_clear_cb);

function _idb_key(channel_id) {
    return 'messages-' + channel_id;
}

function _idb_init_cb(callback) {
    const request = window.indexedDB.open(DATABASE, 4);
    
    request.onerror = (e) => {
        console.error('[ERROR] Could not init IndexedDB!');
        callback(e);
    }

    request.onsuccess = (e) => {
        iDB = e.target.result;
		if (CONFIG_DEBUG_PRINT) {
	        console.log('IndexedDB init success');
		}
        callback(e);
    }

    request.onupgradeneeded = (e) => {
        const db = e.target.result;
        if (!db.objectStoreNames.contains(STORE)) {
            db.createObjectStore(STORE, { 'autoIncrement': true });
        }
    }
}

function _idb_restore_cb(callback, channel_id) {
    const key = _idb_key(channel_id);
    const req = iDB.transaction([STORE], 'readwrite')
        .objectStore(STORE)
        .get(key);

    req.onsuccess = () => { callback(req.result); }
    req.onerror = () => { callback(null); }
}

function _idb_clear_cb(callback) {
    const req = iDB.transaction([STORE], 'readwrite')
        .objectStore(STORE)
        .clear();

    req.onsuccess = callback;
    req.onerror = callback;
}

function _idb_save_cb(callback, channel_id, messages) {
    const key = _idb_key(channel_id);
    const req = iDB.transaction([STORE], 'readwrite')
        .objectStore(STORE)
        .put(messages, key);

    req.onsuccess = callback;
    req.onerror = callback;
}

function imm_storage_sync(channel_id) {
    idb_save(channel_id, messages[channel_id]);
}

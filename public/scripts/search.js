const SEARCH_MIN_QUERY_LENGTH = 2;

let search_on = false;
let search_really_on = false;
let search_query = '';

function search_disable() {
    if (search_on) {
        search_toggle();
    }
}

function search_toggle(on_complete = null) {
    search_on = !search_on; /* JOKE: This is literally always false */
    
    if (search_on) {
        // Turn ON search mode
        divs['search-input'].value = '';
        divs['search-input'].classList.remove('hidden');
        divs['search-input'].focus();
    } else {
        // Turn OFF search mode
        divs['search-input'].classList.add('hidden');
        
        if (search_really_on) {
            set_search_filter(null, on_complete);
            search_really_on = false;
        }
    }
}

function search_perform() {
    const query_lc = search_query.toLowerCase();
    set_search_filter(msg => msg.type === RECORD.MESSAGE && msg.text.toLowerCase().includes(query_lc));
}

function search_keydown(e) {
    if (e.code === 'Enter') {
        search_query = divs['search-input'].value;
        if (search_query.length >= SEARCH_MIN_QUERY_LENGTH) {
            search_perform();
            search_really_on = true;
        }
    } else if (e.code === 'Escape' && !e.shiftKey) {
        search_on = false;
        divs['search-input'].classList.add('hidden');
        
        if (search_really_on) {
            set_search_filter(null);
            search_really_on = false;
        }
    }
}

function set_search_filter(filter, on_complete = null) {
    if (filter !== null) {
        divs['container'].classList.add('search');
        channel_scroller.set_filter(filter, on_complete);
    } else {
        set_default_filter(on_complete);
        divs['container'].classList.remove('search');
    }
}
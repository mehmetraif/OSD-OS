.pragma library

// What was typed on a sign-in form (Jellyfin's and Emby's Connect to
// Server), kept while the app runs, so that back from the form and in again
// finds it as it was left: the server's address, the user name, the method
// chosen. Never a password, which is typed again, and never written to
// disk: the address the server was signed in at is saved by the module
// itself (its server_url).
//
//     var kept = TypedFields.get("com.osdos.emby")
//     TypedFields.keep("com.osdos.emby", { server: url })

var forms = {}

// The fields kept for a module's form: {} for none.
function get(moduleId) {
    return Object.assign({}, forms[moduleId] || {})
}

// These fields of a module's form kept, the others as they were.
function keep(moduleId, fields) {
    var kept = Object.assign({}, forms[moduleId] || {})
    for (var name in fields) {
        if (name !== "password")
            kept[name] = fields[name]
    }
    forms[moduleId] = kept
}

// Every form's fields forgotten (the tests, between cases).
function clear() {
    forms = {}
}

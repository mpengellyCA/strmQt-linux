pragma Singleton
import QtQuick

// Reading a ListModel-ish thing (count + get(i)) out into plain JS values
// (C7). Every page that played a whole model used to grow its own drain loop,
// with its own opinion about null guards; the loops that map rows onto another
// shape (season tabs, badge counts) stay with their pages — only the drains
// that were the same shape everywhere live here.
QtObject {
    // Every row of `model` as a plain JS array, in model order. A null model
    // is an empty array, because a lane that has not loaded yet is not an
    // error worth branching on at every call site.
    function drain(model) {
        const out = [];
        if (!model)
            return out;
        for (let i = 0; i < model.count; ++i)
            out.push(model.get(i));
        return out;
    }

    // The playlist model as picker records — {create, id, name, lower} — so
    // that typing filters a string array rather than re-marshalling several
    // hundred QVariantMaps on every keystroke.
    function playlistRecords(model) {
        const entries = drain(model);
        const out = [];
        for (let i = 0; i < entries.length; ++i) {
            const entry = entries[i];
            const name = entry.name !== undefined ? String(entry.name) : "";
            out.push({
                "create": false,
                "id": entry.itemId !== undefined ? String(entry.itemId) : "",
                "name": name,
                "lower": name.toLowerCase()
            });
        }
        return out;
    }
}

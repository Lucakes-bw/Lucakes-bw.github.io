class Trie {
    constructor() {
        this.root = { children: new Map(), end: false };
    }

    insert(word) {
        let p = this.root;
        for (const c of word) {
            if (!p.children.has(c)) p.children.set(c, { children: new Map(), end: false });
            p = p.children.get(c);
        }
        p.end = true;
    }

    walk(s) {
        let p = this.root;
        for (const c of s) {
            p = p.children.get(c);
            if (!p) return null;
        }
        return p;
    }

    search(word) {
        const p = this.walk(word);
        return !!p && p.end;
    }

    startsWith(prefix) {
        return this.walk(prefix) !== null;
    }
}

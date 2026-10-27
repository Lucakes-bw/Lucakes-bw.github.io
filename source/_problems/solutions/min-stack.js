var MinStack = function() {
    this.st = [];
    this.mins = [];
};

/**
 * @param {number} val
 * @return {void}
 */
MinStack.prototype.push = function(val) {
    this.st.push(val);
    this.mins.push(this.mins.length ? Math.min(val, this.mins[this.mins.length - 1]) : val);
};

MinStack.prototype.pop = function() {
    this.st.pop();
    this.mins.pop();
};

MinStack.prototype.top = function() {
    return this.st[this.st.length - 1];
};

MinStack.prototype.getMin = function() {
    return this.mins[this.mins.length - 1];
};

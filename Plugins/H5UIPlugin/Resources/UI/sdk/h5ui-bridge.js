(function (global) {
	'use strict';

	function emit(eventType, functionName) {
		if (typeof eventType !== 'string' || !eventType) {
			throw new TypeError('H5UI.emit requires a non-empty event type.');
		}
		if (typeof functionName !== 'string' || !functionName) {
			throw new TypeError('H5UI.emit requires a non-empty function name.');
		}
		if (typeof global.__silverUeEmitTyped !== 'function') {
			return false;
		}

		var args = [];
		for (var index = 2; index < arguments.length; index += 1) {
			args.push(arguments[index]);
		}
		global.__silverUeEmitTyped(eventType, functionName, JSON.stringify(args), '');
		return true;
	}

	function on(eventName, callback) {
		global.addEventListener(eventName, callback);
		return function () {
			global.removeEventListener(eventName, callback);
		};
	}

	// Preserve runtime free-move API installed by the H5UI bootstrap.
	var drag = (global.H5UI && global.H5UI.drag) || global.__h5uiDrag || null;

	global.H5UI = Object.freeze({
		emit: emit,
		on: on,
		getData: function (name) { return global.ue.getData(name); },
		setData: function (name, value) { global.ue.setData(name, value); },
		drag: drag
	});
})(window);

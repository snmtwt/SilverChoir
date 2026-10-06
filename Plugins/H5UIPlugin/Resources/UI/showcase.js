(function () {
	'use strict';

	var counterElement = document.getElementById('js-counter');
	var statusElement = document.getElementById('js-status');
	var dynamicHost = document.getElementById('js-dynamic-host');
	var incrementButton = document.getElementById('js-increment');
	var deferredButton = document.getElementById('js-deferred');
	var createNodeButton = document.getElementById('js-create-node');
	var nativeOutput = document.getElementById('native-interaction-output');
	var nativeDialog = document.getElementById('native-dialog');
	var nativeTooltipHost = document.getElementById('native-tooltip-trigger').parentElement.parentElement;
	var nativeTooltipTrigger = document.getElementById('native-tooltip-trigger');
	var nativeAnimationWatch = document.getElementById('native-animation-watch');
	var count = 0;
	var createdNodes = 0;

	function emitToUnreal(name, payload, elementId) {
		if (window.H5UI && typeof window.H5UI.emit === 'function') {
			window.H5UI.emit('Showcase', name, payload, elementId || '');
		} else if (window.ue && typeof window.ue.emit === 'function') {
			window.ue.emit(name, payload, elementId);
		}
	}

	function setStatus(message, stateClass) {
		statusElement.textContent = message;
		statusElement.classList.remove('is-ready', 'is-pending');
		if (stateClass) {
			statusElement.classList.add(stateClass);
		}
	}

	function setNativeOutput(message, payload) {
		nativeOutput.textContent = message;
		emitToUnreal('ShowcaseNativeHtmlEvent', payload || { message: message }, 'native-interactions');
	}

	incrementButton.addEventListener('click', function () {
		count += 1;
		counterElement.textContent = String(count);
		counterElement.dataset.count = String(count);
		setStatus('Click event handled by JavaScript', 'is-ready');
		emitToUnreal('JavaScriptCounterChanged', { count: count }, 'js-increment');
	});

	deferredButton.addEventListener('click', function () {
		setStatus('Timer is waiting...', 'is-pending');
		setTimeout(function () {
			setStatus('Timer completed', 'is-ready');
			emitToUnreal('JavaScriptTimerCompleted', { delayMilliseconds: 60 }, 'js-deferred');
		}, 60);
	});

	createNodeButton.addEventListener('click', function () {
		createdNodes += 1;
		if (createdNodes === 1) {
			dynamicHost.textContent = '';
		}
		var chip = document.createElement('span');
		chip.id = 'js-created-' + String(createdNodes);
		chip.className = 'javascript-chip';
		chip.textContent = 'Node ' + String(createdNodes);
		dynamicHost.appendChild(chip);
		setStatus('DOM node created by JavaScript', 'is-ready');
		emitToUnreal('JavaScriptNodeCreated', { id: chip.id }, 'js-create-node');
	});

	['native-shield', 'native-repair', 'native-interaction-select', 'native-color'].forEach(function (id) {
		var input = document.getElementById(id);
		input.addEventListener('change', function () {
			setNativeOutput(id + ' changed to ' + (input.value || String(input.checked)), { element: id, value: input.value || String(input.checked) });
		});
	});

	document.getElementById('native-open-dialog').addEventListener('click', function () {
		nativeDialog.classList.add('open');
		setNativeOutput('Native dialog opened', { element: 'native-dialog', open: true });
	});
	document.getElementById('native-close-dialog').addEventListener('click', function () {
		nativeDialog.classList.remove('open');
		setNativeOutput('Native dialog closed', { element: 'native-dialog', open: false });
	});

	nativeTooltipTrigger.addEventListener('click', function () {
		nativeTooltipHost.classList.toggle('tooltip-open');
		setNativeOutput('Native tooltip toggled', { element: 'native-tooltip', open: nativeTooltipHost.classList.contains('tooltip-open') });
	});
	nativeAnimationWatch.addEventListener('animationstart', function () {
		setNativeOutput('Native div received animationstart', { element: 'native-animation-watch', event: 'animationstart' });
	});

	setStatus('JavaScript ready', 'is-ready');
	statusElement.dataset.runtime = window.ue ? 'h5-ui-plugin' : 'browser';

	requestAnimationFrame(function (timestamp) {
		statusElement.dataset.firstFrame = String(Math.round(timestamp));
	});
})();

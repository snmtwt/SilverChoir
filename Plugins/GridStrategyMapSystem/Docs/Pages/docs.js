(function () {
	function inPagesDirectory() {
		return /\/Pages\//i.test(window.location.pathname.replace(/\\/g, "/"));
	}

	function href(fileName) {
		return inPagesDirectory() ? fileName : "Pages/" + fileName;
	}

	function rootHref() {
		return inPagesDirectory() ? "../说明文档.html" : "说明文档.html";
	}

	function currentFileName() {
		var path = decodeURIComponent(window.location.pathname.replace(/\\/g, "/"));
		return path.substring(path.lastIndexOf("/") + 1) || "说明文档.html";
	}

	function link(fileName, label) {
		var current = currentFileName();
		var active = current === fileName || (fileName === "说明文档.html" && current === "");
		var target = fileName === "说明文档.html" ? rootHref() : href(fileName);
		return '<a class="nav-link' + (active ? ' active' : '') + '" href="' + target + '">' + label + '</a>';
	}

	function renderSidebar() {
		var sidebar = document.querySelector('.sidebar');
		if (!sidebar) {
			return;
		}

		sidebar.innerHTML =
			'<h1>GSM Map System</h1>' +
			'<p class="version">数据与展示分离架构</p>' +
			'<div class="nav-group">' +
			link('说明文档.html', '总览与快速接入') +
			link('data-model.html', '数据模型与配置') +
			link('blueprint-api.html', '蓝图 API') +
			link('display.html', '2D / 3D 展示') +
			link('lifecycle.html', '读档与生命周期') +
			'</div>' +
			'<div class="nav-group"><a class="nav-link" href="' + (inPagesDirectory() ? '../README.md' : 'README.md') + '">完整 Markdown 文档</a></div>';
	}

	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', renderSidebar);
	} else {
		renderSidebar();
	}
}());

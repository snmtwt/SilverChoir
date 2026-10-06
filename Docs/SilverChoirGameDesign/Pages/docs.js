(function () {
	var ROOT_PAGE = "游戏策划文档.html";
	var STORAGE_KEY = "SilverChoirGameDesign.OpenNavBranches";

	var NAV_TREE = [
		{ title: "总览", page: ROOT_PAGE, root: true },
		{
			title: "产品定义",
			children: [
				{ title: "设计愿景与范围", page: "vision-scope.html" }
			]
		},
		{
			title: "核心体验",
			children: [
				{ title: "核心循环", page: "core-loop.html" },
				{ title: "玩家目标与成长", page: "player-goals-progression.html" },
				{ title: "游戏流程与状态", page: "game-flow-states.html" }
			]
		},
		{
			title: "世界与叙事",
			children: [
				{ title: "世界观与规则", page: "world-setting.html" },
				{
					title: "阵营与角色",
					children: [
						{ title: "阵营概览", page: "factions-characters.html" },
						{ title: "银色唱诗班", page: "faction-silver-choir.html" },
						{ title: "夜莺联合会", page: "faction-nightingale.html" },
						{ title: "卡洛维家族", page: "faction-cartel.html" },
						{ title: "满堂会", page: "faction-fullhouse.html" },
						{ title: "灰钥协议", page: "faction-darkweb.html" },
						{ title: "莫尔坎庄园", page: "faction-manor.html" },
						{ title: "奥特拉集团", page: "faction-tech-group.html" },
						{ title: "安图恩复国阵线", page: "faction-restoration-army.html" },
						{ title: "安图恩城市警卫总署", page: "faction-security-bureau.html" },
						{ title: "政府军", page: "faction-government-army.html" }
					]
				},
				{ title: "叙事、剧情与任务", page: "narrative-missions.html" }
			]
		},
		{
			title: "剧情设计",
			children: [
				{ title: "设计思路", page: "story-design.html" },
				{ title: "序章", page: "story-design.html#prologue" },
				{ title: "第一章", page: "story-design.html#chapter-one" }
			]
		},
		{
			title: "战略经营层",
			children: [
				{ title: "Haven 基地", page: "haven-base.html" },
				{ title: "战略地图与时间", page: "strategic-map-time.html" },
				{ title: "任务与事件", page: "missions-events.html" },
				{ title: "资源与经济", page: "economy-resources.html" }
			]
		},
		{
			title: "单位与小队",
			children: [
				{ title: "单位、小队与编成", page: "units-squads.html" },
				{ title: "成长、装备与载具", page: "growth-equipment-vehicles.html" }
			]
		},
		{
			title: "战术战斗层",
			children: [
				{ title: "战斗循环与规则", page: "combat-rules.html" },
				{ title: "战术地图与目标", page: "tactical-maps-objectives.html" },
				{ title: "敌人与 AI", page: "enemies-ai.html" }
			]
		},
		{
			title: "内容与数值",
			children: [
				{ title: "内容目录与模板", page: "content-catalog.html" },
				{ title: "平衡、难度与奖励", page: "balance-difficulty-rewards.html" }
			]
		},
		{
			title: "体验与表现",
			children: [
				{ title: "UI、操作与无障碍", page: "ui-controls-accessibility.html" },
				{ title: "美术与音频方向", page: "art-audio-direction.html" }
			]
		},
		{
			title: "制作与验证",
			children: [
				{ title: "范围与里程碑", page: "production-milestones.html" },
				{ title: "内容生产管线", page: "content-pipeline.html" },
				{ title: "测试与指标", page: "playtest-metrics.html" },
				{ title: "风险与决策记录", page: "risks-decisions.html" },
				{ title: "术语与变更记录", page: "glossary-changelog.html" }
			]
		}
	];

	function isPageDir() {
		return /\/Pages\//i.test(window.location.pathname.replace(/\\/g, "/"));
	}

	function currentFile() {
		var path = decodeURIComponent(window.location.pathname.replace(/\\/g, "/"));
		return path.substring(path.lastIndexOf("/") + 1) || ROOT_PAGE;
	}

	function currentFileEditHref() {
		if (window.location.protocol !== "file:") {
			return "";
		}

		var path = decodeURIComponent(window.location.pathname.replace(/\\/g, "/"));
		path = path.replace(/^\/([A-Za-z]:\/)/, "$1");
		return encodeURI("vscode://file/" + path);
	}

	function hrefFor(node) {
		if (node.root) {
			return isPageDir() ? "../" + ROOT_PAGE : ROOT_PAGE;
		}
		return isPageDir() ? node.page : "Pages/" + node.page;
	}

	function isActive(node) {
		if (!node.page) {
			return false;
		}

		var hashIndex = node.page.indexOf("#");
		var page = hashIndex >= 0 ? node.page.substring(0, hashIndex) : node.page;
		var hash = hashIndex >= 0 ? node.page.substring(hashIndex) : "";
		return currentFile() === page && (!hash || window.location.hash === hash);
	}

	function containsActive(nodes) {
		return nodes.some(function (node) {
			return isActive(node) || (node.children && containsActive(node.children));
		});
	}

	function loadOpenBranches() {
		try {
			return JSON.parse(window.localStorage.getItem(STORAGE_KEY) || "[]");
		} catch (error) {
			return [];
		}
	}

	function persistOpenBranches() {
		try {
			var ids = Array.prototype.slice.call(document.querySelectorAll("details.nav-accordion[open]"))
				.map(function (item) { return item.getAttribute("data-branch-id"); })
				.filter(Boolean);
			window.localStorage.setItem(STORAGE_KEY, JSON.stringify(ids));
		} catch (error) {
			// The navigation remains usable when local storage is unavailable.
		}
	}

	function renderNodes(container, nodes, level, parentId) {
		var openBranches = loadOpenBranches();
		nodes.forEach(function (node, index) {
			var branchId = (parentId ? parentId + "/" : "") + index + ":" + node.title;
			if (node.children && node.children.length) {
				var details = document.createElement("details");
				var openPath = containsActive(node.children);
				details.className = "nav-accordion" + (openPath ? " open-path" : "");
				details.setAttribute("data-branch-id", branchId);
				details.style.setProperty("--nav-indent", level * 13 + "px");
				details.open = openPath || openBranches.indexOf(branchId) >= 0;

				var summary = document.createElement("summary");
				summary.textContent = node.title;
				details.appendChild(summary);
				renderNodes(details, node.children, level + 1, branchId);
				container.appendChild(details);
				return;
			}

			var link = document.createElement("a");
			link.className = "nav-link" + (isActive(node) ? " active" : "");
			link.href = hrefFor(node);
			link.textContent = node.title;
			link.style.setProperty("--nav-indent", level * 13 + "px");
			container.appendChild(link);
		});
	}

	function renderSidebar() {
		var sidebar = document.querySelector(".sidebar");
		if (!sidebar) {
			return;
		}
		sidebar.innerHTML = "";

		var title = document.createElement("h1");
		title.textContent = "SilverChoir GDD";
		sidebar.appendChild(title);

		var version = document.createElement("p");
		version.className = "version";
		version.textContent = "游戏策划文档 · 初始框架";
		sidebar.appendChild(version);

		var nav = document.createElement("div");
		nav.className = "nav-group nav-tree";
		renderNodes(nav, NAV_TREE, 0, "");
		sidebar.appendChild(nav);

		var headings = Array.prototype.slice.call(document.querySelectorAll("main section[id] > h2"));
		if (headings.length) {
			var toc = document.createElement("div");
			toc.className = "nav-group page-toc";
			var tocTitle = document.createElement("p");
			tocTitle.className = "nav-title";
			tocTitle.textContent = "本页目录";
			toc.appendChild(tocTitle);
			headings.forEach(function (heading) {
				var link = document.createElement("a");
				link.className = "nav-link";
				link.href = "#" + heading.parentElement.id;
				link.textContent = heading.textContent;
				toc.appendChild(link);
			});
			sidebar.appendChild(toc);
		}
	}

	function renderPageFilename() {
		var main = document.querySelector("main");
		if (!main || main.querySelector(".page-filename")) {
			return;
		}

		var label = document.createElement("a");
		label.className = "page-filename";
		label.textContent = currentFile();
		var editHref = currentFileEditHref();
		if (editHref) {
			label.href = editHref;
			label.title = "Open this HTML in VS Code";
		}
		main.insertBefore(label, main.firstElementChild);
	}

	function initializeDocument() {
		renderSidebar();
		renderPageFilename();
	}

	document.addEventListener("toggle", function (event) {
		if (event.target.matches("details.nav-accordion")) {
			persistOpenBranches();
		}
	}, true);

	if (document.readyState === "loading") {
		document.addEventListener("DOMContentLoaded", initializeDocument);
	} else {
		initializeDocument();
	}
}());

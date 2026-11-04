(function () {
  "use strict";

  // Contents menu on narrow screens
  var toggle = document.querySelector(".menu-toggle");
  if (toggle) {
    toggle.addEventListener("click", function () {
      var open = document.body.classList.toggle("nav-open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  // Light / dark switch (remembered in this browser when storage is available)
  var theme = document.querySelector(".theme-toggle");
  if (theme) {
    theme.addEventListener("click", function () {
      var root = document.documentElement;
      var current = root.getAttribute("data-theme") ||
        (window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
      var next = current === "dark" ? "light" : "dark";
      root.setAttribute("data-theme", next);
      try { localStorage.setItem("oscar-guide-theme", next); } catch (e) {}
    });
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }
  function escapeRe(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"); }
  function words(q) {
    return q.toLowerCase().split(/[^\p{L}\p{N}.+-]+/u).filter(function (w) { return w.length > 0; });
  }

  // Index page filter
  var filter = document.getElementById("index-filter");
  if (filter) {
    filter.addEventListener("input", function () {
      var q = filter.value.trim().toLowerCase();
      document.querySelectorAll(".index-entry").forEach(function (e) {
        e.style.display = !q || e.textContent.toLowerCase().indexOf(q) >= 0 ? "" : "none";
      });
      document.querySelectorAll(".index-letter").forEach(function (sec) {
        var any = Array.prototype.some.call(sec.querySelectorAll(".index-entry"), function (e) { return e.style.display !== "none"; });
        sec.style.display = any ? "" : "none";
      });
    });
  }

  // Search page
  var results = document.getElementById("search-results");
  if (!results || !window.OSCAR_SEARCH) return;
  var params = new URLSearchParams(window.location.search);
  var query = (params.get("q") || "").trim();
  var input = document.getElementById("search-page-input");
  if (input) { input.value = query; input.focus(); }
  document.querySelectorAll(".search-box input").forEach(function (i) { i.value = query; });
  if (!query) { results.innerHTML = "<p>Type one or more words. Every word must appear in a section for it to match.</p>"; return; }

  var terms = words(query);
  var phrase = query.toLowerCase();
  var hits = [];
  window.OSCAR_SEARCH.forEach(function (doc) {
    var title = doc.t.toLowerCase(), text = doc.x.toLowerCase(), score = 0;
    for (var i = 0; i < terms.length; i++) {
      var t = terms[i], inTitle = title.indexOf(t) >= 0, inText = text.indexOf(t) >= 0;
      if (!inTitle && !inText) return;
      if (inTitle) score += 10;
      var re = new RegExp(escapeRe(t), "g"), m = text.match(re);
      score += m ? Math.min(m.length, 10) : 0;
    }
    if (terms.length > 1 && (title + " " + text).indexOf(phrase) >= 0) score += 15;
    hits.push({ doc: doc, score: score });
  });
  hits.sort(function (a, b) { return b.score - a.score; });

  var highlight = new RegExp("(" + terms.map(escapeRe).join("|") + ")", "gi");
  function snippet(text) {
    var lower = text.toLowerCase(), pos = -1;
    for (var i = 0; i < terms.length && pos < 0; i++) pos = lower.indexOf(terms[i]);
    var start = Math.max(0, pos - 80), end = Math.min(text.length, start + 260);
    var s = (start > 0 ? "… " : "") + text.slice(start, end) + (end < text.length ? " …" : "");
    return escapeHtml(s).replace(highlight, "<mark>$1</mark>");
  }

  var html = "<p>" + hits.length + (hits.length === 1 ? " section matches" : " sections match") +
    " <strong>" + escapeHtml(query) + "</strong>.</p>";
  hits.slice(0, 60).forEach(function (h) {
    var where = h.doc.t === h.doc.p ? "" : '<div class="where">' + escapeHtml(h.doc.p) + "</div>";
    html += '<div class="search-result"><a href="' + h.doc.u + '">' + escapeHtml(h.doc.t).replace(highlight, "<mark>$1</mark>") +
      "</a>" + where + "<p>" + snippet(h.doc.x) + "</p></div>";
  });
  if (!hits.length) html += "<p>Try fewer words, or look the topic up in the <a href=\"genindex.html\">Index</a>.</p>";
  results.innerHTML = html;
})();

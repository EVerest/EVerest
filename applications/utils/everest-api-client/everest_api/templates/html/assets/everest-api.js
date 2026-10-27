// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
// Shared script of the EVerest API reference pages. The pages work without
// it; it adds expand/collapse all, filtering, copy buttons and a theme switch.

(function () {
  'use strict';

  var root = document.documentElement;
  root.classList.add('js');

  function storageGet(key) {
    try {
      return window.localStorage.getItem(key);
    } catch (e) {
      return null;
    }
  }
  function storageSet(key, value) {
    try {
      window.localStorage.setItem(key, value);
    } catch (e) {
      /* storage unavailable */
    }
  }

  // ---- theme: auto -> light -> dark ----
  var THEMES = ['auto', 'light', 'dark'];
  function applyTheme(theme) {
    if (theme === 'light' || theme === 'dark') {
      root.setAttribute('data-theme', theme);
    } else {
      root.removeAttribute('data-theme');
      theme = 'auto';
    }
    document.querySelectorAll('[data-theme-toggle]').forEach(function (button) {
      button.textContent = 'Theme: ' + theme;
    });
    return theme;
  }
  var currentTheme = applyTheme(storageGet('everest-api-theme'));

  // ---- full-width layout ----
  function applyWidth(full) {
    if (full) {
      root.setAttribute('data-width', 'full');
    } else {
      root.removeAttribute('data-width');
    }
    document.querySelectorAll('[data-width-toggle]').forEach(function (button) {
      button.setAttribute('aria-pressed', full ? 'true' : 'false');
    });
    return full;
  }
  var fullWidth = applyWidth(storageGet('everest-api-width') === 'full');

  // ---- highlight the sidebar entry of what is on screen ----
  function watchCurrentSection() {
    if (!('IntersectionObserver' in window)) return;
    var links = {};
    document.querySelectorAll(".toc a[href^='#']").forEach(function (a) {
      links[decodeURIComponent(a.getAttribute('href').slice(1))] = a;
    });
    var targets = Array.prototype.filter.call(
      document.querySelectorAll('main > section, article.op, details.schema'),
      function (el) {
        return links[el.id];
      },
    );
    var visible = new Set();
    var current = [];
    var observer = new IntersectionObserver(
      function (entries) {
        entries.forEach(function (entry) {
          if (entry.isIntersecting) visible.add(entry.target);
          else visible.delete(entry.target);
        });
        current.forEach(function (a) {
          a.removeAttribute('aria-current');
        });
        current = [];
        // the topmost visible section, and within it the topmost visible item
        var section = null,
          item = null;
        targets.forEach(function (el) {
          if (!visible.has(el)) return;
          if (el.tagName === 'SECTION') {
            if (!section) section = el;
          } else if (!item) {
            item = el;
          }
        });
        [section, item].forEach(function (el) {
          if (el) {
            links[el.id].setAttribute('aria-current', 'true');
            current.push(links[el.id]);
          }
        });
      },
      { rootMargin: '-25% 0px -60% 0px' },
    );
    targets.forEach(function (el) {
      observer.observe(el);
    });
  }

  // ---- open the <details> around a linked target ----
  function revealHash() {
    var id = decodeURIComponent(window.location.hash.slice(1));
    if (!id) return;
    var target = document.getElementById(id);
    if (!target) return;
    for (var el = target; el; el = el.parentElement) {
      if (el.tagName === 'DETAILS') el.open = true;
    }
    target.scrollIntoView();
  }

  // A schema defined on this page is rendered once in the schemas section;
  // tree nodes referring to it copy that definition in when first opened.
  function inlineDefinition(details) {
    var placeholder = details.querySelector(':scope > .s-inline');
    if (!placeholder) return;
    var source = document.querySelector(
      '#' + details.getAttribute('data-inline') + ' > .s-root',
    );
    if (!source) return;
    var hasUseSiteDescription =
      details.querySelector(':scope > .s-desc') !== null;
    Array.prototype.forEach.call(source.children, function (child) {
      if (child.classList.contains('s-head')) return;
      if (hasUseSiteDescription && child.classList.contains('s-desc')) return;
      details.insertBefore(child.cloneNode(true), placeholder);
    });
    placeholder.remove();
    details.querySelectorAll('details[data-inline]').forEach(watchInline);
  }

  function watchInline(details) {
    if (details.open) inlineDefinition(details);
    details.addEventListener('toggle', function () {
      if (details.open) inlineDefinition(details);
    });
  }

  // A definition already being shown by an ancestor is a recursive
  // reference; leave it closed so expanding terminates.
  function isRecursive(details) {
    var target = details.getAttribute('data-inline');
    for (var el = details.parentElement; el; el = el.parentElement) {
      if (el.tagName === 'DETAILS' && el.getAttribute('data-inline') === target)
        return true;
    }
    return false;
  }

  function expandAll(container) {
    container
      .querySelectorAll('details:not([data-inline])')
      .forEach(function (d) {
        d.open = true;
      });
    var pending;
    do {
      pending = Array.prototype.filter.call(
        container.querySelectorAll('details[data-inline]:not([open])'),
        function (d) {
          return !isRecursive(d);
        },
      );
      pending.forEach(function (d) {
        d.open = true;
        inlineDefinition(d);
        d.querySelectorAll('details:not([data-inline])').forEach(
          function (inner) {
            inner.open = true;
          },
        );
      });
    } while (pending.length);
  }

  function addCopyButton(container, getText) {
    if (!navigator.clipboard) return;
    var button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy';
    button.textContent = 'Copy';
    button.addEventListener('click', function (event) {
      event.preventDefault();
      navigator.clipboard.writeText(getText()).then(function () {
        button.textContent = 'Copied';
        setTimeout(function () {
          button.textContent = 'Copy';
        }, 1200);
      });
    });
    container.appendChild(button);
  }

  function init() {
    applyTheme(currentTheme);
    document.querySelectorAll('[data-theme-toggle]').forEach(function (button) {
      button.addEventListener('click', function () {
        currentTheme = applyTheme(
          THEMES[(THEMES.indexOf(currentTheme) + 1) % THEMES.length],
        );
        storageSet('everest-api-theme', currentTheme);
      });
    });

    applyWidth(fullWidth);
    document.querySelectorAll('[data-width-toggle]').forEach(function (button) {
      button.addEventListener('click', function () {
        fullWidth = applyWidth(!fullWidth);
        storageSet('everest-api-width', fullWidth ? 'full' : 'normal');
      });
    });

    // On narrow screens the contents list sits above the page; start it
    // collapsed there so the page itself is what shows first.
    var narrow = window.matchMedia('(max-width: 900px)');
    document.querySelectorAll('.nav-panel').forEach(function (panel) {
      panel.open = !narrow.matches;
      narrow.addEventListener('change', function () {
        panel.open = !narrow.matches;
      });
      panel.addEventListener('click', function (event) {
        if (narrow.matches && event.target.closest('a')) panel.open = false;
      });
    });

    watchCurrentSection();

    var main = document.getElementById('main');
    main.querySelectorAll('details[data-inline]').forEach(watchInline);
    document.querySelectorAll('[data-expand]').forEach(function (button) {
      button.addEventListener('click', function () {
        if (button.getAttribute('data-expand') === 'all') {
          expandAll(main);
        } else {
          main.querySelectorAll('details').forEach(function (d) {
            d.open = false;
          });
        }
      });
    });

    document.querySelectorAll('code[data-copy]').forEach(function (code) {
      var holder = document.createElement('span');
      code.parentNode.insertBefore(holder, code.nextSibling);
      addCopyButton(holder, function () {
        return code.getAttribute('data-copy');
      });
    });
    document.querySelectorAll('pre[data-copy-block]').forEach(function (pre) {
      addCopyButton(pre, function () {
        return pre.querySelector('code').textContent;
      });
    });

    var input = document.querySelector('[data-filter-input]');
    if (input) {
      var items = document.querySelectorAll('[data-filter]');
      input.addEventListener('input', function () {
        var terms = input.value.toLowerCase().split(/\s+/).filter(Boolean);
        items.forEach(function (item) {
          var text = item.getAttribute('data-filter').toLowerCase();
          item.hidden = !terms.every(function (t) {
            return text.indexOf(t) !== -1;
          });
        });
      });
    }

    revealHash();
    window.addEventListener('hashchange', revealHash);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();

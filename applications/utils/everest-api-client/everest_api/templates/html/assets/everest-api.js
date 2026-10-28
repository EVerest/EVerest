// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
// Shared script of the EVerest API reference pages. The pages work without
// it; it adds expand/collapse all, filtering, copy buttons and a theme switch.

/* eslint-disable strict */

(function everestApi() {
  // Loaded as a classic script (a module would not load from file:// pages),
  // so the directive is not redundant here.

  'use strict';

  const root = document.documentElement;
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
  const THEMES = ['auto', 'light', 'dark'];
  function applyTheme(requested) {
    const explicit = requested === 'light' || requested === 'dark';
    const theme = explicit ? requested : 'auto';
    if (explicit) {
      root.setAttribute('data-theme', theme);
    } else {
      root.removeAttribute('data-theme');
    }
    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
      button.replaceChildren(`Theme: ${theme}`);
    });
    return theme;
  }
  let currentTheme = applyTheme(storageGet('everest-api-theme'));

  // ---- full-width layout ----
  function applyWidth(full) {
    if (full) {
      root.setAttribute('data-width', 'full');
    } else {
      root.removeAttribute('data-width');
    }
    document.querySelectorAll('[data-width-toggle]').forEach((button) => {
      button.setAttribute('aria-pressed', full ? 'true' : 'false');
    });
    return full;
  }
  let fullWidth = applyWidth(storageGet('everest-api-width') === 'full');

  // ---- highlight the sidebar entry of what is on screen ----
  function watchCurrentSection() {
    if (!('IntersectionObserver' in window)) return;
    const links = {};
    document.querySelectorAll(".toc a[href^='#']").forEach((a) => {
      links[decodeURIComponent(a.getAttribute('href').slice(1))] = a;
    });
    const targets = Array.prototype.filter.call(
      document.querySelectorAll('main > section, article.op, details.schema'),
      (el) => links[el.id]
    );
    const visible = new Set();
    let current = [];
    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) visible.add(entry.target);
          else visible.delete(entry.target);
        });
        current.forEach((a) => {
          a.removeAttribute('aria-current');
        });
        current = [];
        // the topmost visible section, and within it the topmost visible item
        let section = null;
        let item = null;
        targets.forEach((el) => {
          if (!visible.has(el)) return;
          if (el.tagName === 'SECTION') {
            if (!section) section = el;
          } else if (!item) {
            item = el;
          }
        });
        [section, item].forEach((el) => {
          if (el) {
            links[el.id].setAttribute('aria-current', 'true');
            current.push(links[el.id]);
          }
        });
      },
      { rootMargin: '-25% 0px -60% 0px' }
    );
    targets.forEach((el) => {
      observer.observe(el);
    });
  }

  // ---- open the <details> around a linked target ----
  function revealHash() {
    const id = decodeURIComponent(window.location.hash.slice(1));
    if (!id) return;
    const target = document.getElementById(id);
    if (!target) return;
    for (let el = target; el; el = el.parentElement) {
      if (el.tagName === 'DETAILS') el.open = true;
    }
    target.scrollIntoView();
  }

  // A schema defined on this page is rendered once in the schemas section;
  // tree nodes referring to it copy that definition in when first opened.
  function inlineDefinition(details) {
    const placeholder = details.querySelector(':scope > .s-inline');
    if (!placeholder) return;
    const source = document.querySelector(
      `#${details.getAttribute('data-inline')} > .s-root`
    );
    if (!source) return;
    const hasUseSiteDescription = details.querySelector(':scope > .s-desc') !== null;
    Array.prototype.forEach.call(source.children, (child) => {
      if (child.classList.contains('s-head')) return;
      if (hasUseSiteDescription && child.classList.contains('s-desc')) return;
      details.insertBefore(child.cloneNode(true), placeholder);
    });
    placeholder.remove();
    // watchInline and inlineDefinition call each other
    // eslint-disable-next-line no-use-before-define
    details.querySelectorAll('details[data-inline]').forEach(watchInline);
  }

  function watchInline(details) {
    if (details.open) inlineDefinition(details);
    details.addEventListener('toggle', () => {
      if (details.open) inlineDefinition(details);
    });
  }

  // A definition already being shown by an ancestor is a recursive
  // reference; leave it closed so expanding terminates.
  function isRecursive(details) {
    const target = details.getAttribute('data-inline');
    for (let el = details.parentElement; el; el = el.parentElement) {
      if (el.tagName === 'DETAILS' && el.getAttribute('data-inline') === target) return true;
    }
    return false;
  }

  function expandAll(container) {
    container
      .querySelectorAll('details:not([data-inline])')
      .forEach((d) => {
        d.toggleAttribute('open', true);
      });
    let pending;
    do {
      pending = Array.prototype.filter.call(
        container.querySelectorAll('details[data-inline]:not([open])'),
        (d) => !isRecursive(d)
      );
      pending.forEach((d) => {
        d.toggleAttribute('open', true);
        inlineDefinition(d);
        d.querySelectorAll('details:not([data-inline])').forEach(
          (inner) => {
            inner.toggleAttribute('open', true);
          }
        );
      });
    } while (pending.length);
  }

  function addCopyButton(container, getText) {
    if (!navigator.clipboard) return;
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy';
    button.textContent = 'Copy';
    button.addEventListener('click', (event) => {
      event.preventDefault();
      navigator.clipboard.writeText(getText()).then(() => {
        button.textContent = 'Copied';
        setTimeout(() => {
          button.textContent = 'Copy';
        }, 1200);
      });
    });
    container.appendChild(button);
  }

  function init() {
    applyTheme(currentTheme);
    document.querySelectorAll('[data-theme-toggle]').forEach((button) => {
      button.addEventListener('click', () => {
        currentTheme = applyTheme(
          THEMES[(THEMES.indexOf(currentTheme) + 1) % THEMES.length]
        );
        storageSet('everest-api-theme', currentTheme);
      });
    });

    applyWidth(fullWidth);
    document.querySelectorAll('[data-width-toggle]').forEach((button) => {
      button.addEventListener('click', () => {
        fullWidth = applyWidth(!fullWidth);
        storageSet('everest-api-width', fullWidth ? 'full' : 'normal');
      });
    });

    // On narrow screens the contents list sits above the page; start it
    // collapsed there so the page itself is what shows first.
    const narrow = window.matchMedia('(max-width: 900px)');
    document.querySelectorAll('.nav-panel').forEach((panel) => {
      panel.toggleAttribute('open', !narrow.matches);
      narrow.addEventListener('change', () => {
        panel.toggleAttribute('open', !narrow.matches);
      });
      panel.addEventListener('click', (event) => {
        if (narrow.matches && event.target.closest('a')) panel.toggleAttribute('open', false);
      });
    });

    watchCurrentSection();

    const main = document.getElementById('main');
    main.querySelectorAll('details[data-inline]').forEach(watchInline);
    document.querySelectorAll('[data-expand]').forEach((button) => {
      button.addEventListener('click', () => {
        if (button.getAttribute('data-expand') === 'all') {
          expandAll(main);
        } else {
          main.querySelectorAll('details').forEach((d) => {
            d.toggleAttribute('open', false);
          });
        }
      });
    });

    document.querySelectorAll('code[data-copy]').forEach((code) => {
      const holder = document.createElement('span');
      code.parentNode.insertBefore(holder, code.nextSibling);
      addCopyButton(holder, () => code.getAttribute('data-copy'));
    });
    document.querySelectorAll('pre[data-copy-block]').forEach((pre) => {
      addCopyButton(pre, () => pre.querySelector('code').textContent);
    });

    const input = document.querySelector('[data-filter-input]');
    if (input) {
      const items = document.querySelectorAll('[data-filter]');
      input.addEventListener('input', () => {
        const terms = input.value.toLowerCase().split(/\s+/).filter(Boolean);
        items.forEach((item) => {
          const text = item.getAttribute('data-filter').toLowerCase();
          item.toggleAttribute(
            'hidden',
            !terms.every((t) => text.indexOf(t) !== -1)
          );
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
}());

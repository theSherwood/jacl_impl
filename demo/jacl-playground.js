// JACL Playground — WASM integration and UI logic

// === State ===
let jacl = null;      // Emscripten module instance
let vm = null;        // Current JaclVM pointer
let outputLines = []; // Captured print output for current run
let examples = [];    // Loaded example files

// === DOM refs ===
const editor = document.getElementById('editor');
const output = document.getElementById('output');
const runBtn = document.getElementById('run-btn');
const clearBtn = document.getElementById('clear-btn');
const status = document.getElementById('status');
const statusRight = document.getElementById('status-right');
const searchInput = document.getElementById('example-search');
const dropdown = document.getElementById('example-dropdown');

// === Default code ===
const DEFAULT_CODE = `# Welcome to JACL!
# Edit this code and click Run (or Ctrl+Enter)

# Basic arithmetic
print [+ 10 20]
print [* 6 7]

# Variables
def msg "hello, world"
print $msg

# Procedures
proc fact {n} {
  if [== $n 0] { 1 } else { [* $n [fact [- $n 1]]] }
}
print [fact 10]

# Vectors
def nums [vec 1 2 3 4 5]
print $nums

# Closures
proc adder {x} {
  proc inner {y} { [+ $x $y] }
}
def add10 [adder 10]
print [add10 32]
`;

// === WASM Integration ===

function withCString(str, fn) {
  const len = jacl.lengthBytesUTF8(str) + 1;
  const ptr = jacl._malloc(len);
  jacl.stringToUTF8(str, ptr, len);
  const result = fn(ptr);
  jacl._free(ptr);
  return result;
}

function runCode(source) {
  outputLines = [];

  // Fresh VM per run
  if (vm) {
    jacl._jacl_vm_free(vm);
    vm = null;
  }
  vm = jacl._jacl_vm_new();

  const result = withCString(source, (ptr) => jacl._jacl_eval(vm, ptr));
  const isError = jacl._jacl_is_error_val(result);
  let errorMessage = null;

  if (isError) {
    const msgPtr = jacl._jacl_error_message_str(vm, result);
    if (msgPtr) {
      errorMessage = jacl.UTF8ToString(msgPtr);
    } else {
      errorMessage = 'Unknown error';
    }
  }

  return { output: outputLines.join('\n'), error: errorMessage, isError };
}

async function initWasm() {
  try {
    jacl = await createJACL({
      print: (text) => { outputLines.push(text); },
      printErr: (text) => { outputLines.push(text); },
    });
    setStatus('Ready', 'ok');
    runBtn.disabled = false;
    output.innerHTML = '<span class="placeholder">Press Run or Ctrl+Enter to execute</span>';
  } catch (e) {
    setStatus('WASM load failed: ' + e.message, 'error');
    output.innerHTML = '<span class="error-line">Failed to load WASM module.\n\n' +
      'Make sure you ran: bash build_demo.sh\n' +
      'And are serving via HTTP (not file://)</span>';
  }
}

// === Example Picker ===

async function loadExamples() {
  try {
    const resp = await fetch('examples.json');
    examples = await resp.json();
    searchInput.placeholder = `Search ${examples.length} examples...`;
  } catch (e) {
    searchInput.placeholder = 'Examples unavailable';
    searchInput.disabled = true;
  }
}

function renderDropdown(filter) {
  dropdown.innerHTML = '';
  const query = (filter || '').toLowerCase();

  // Filter examples
  const filtered = query
    ? examples.filter(ex =>
        ex.name.toLowerCase().includes(query) ||
        ex.category.toLowerCase().includes(query) ||
        (ex.description && ex.description.toLowerCase().includes(query)))
    : examples;

  if (filtered.length === 0) {
    dropdown.innerHTML = '<div class="dropdown-item" style="color:var(--text-dim)">No matches</div>';
    return;
  }

  // Group by category
  const groups = new Map();
  for (const ex of filtered) {
    if (!groups.has(ex.category)) groups.set(ex.category, []);
    groups.get(ex.category).push(ex);
  }

  let itemIndex = 0;
  for (const [cat, items] of groups) {
    const header = document.createElement('div');
    header.className = 'dropdown-category';
    header.textContent = `${cat} (${items.length})`;
    dropdown.appendChild(header);

    for (const ex of items) {
      const item = document.createElement('div');
      item.className = 'dropdown-item';
      item.dataset.index = itemIndex++;
      item.dataset.name = ex.name;

      let label = ex.name;
      if (ex.concurrent) {
        label += ' <span class="badge badge-concurrent">async</span>';
      }
      item.innerHTML = label;

      item.addEventListener('click', () => selectExample(ex));
      dropdown.appendChild(item);
    }
  }
}

function selectExample(ex) {
  editor.value = ex.code;
  searchInput.value = '';
  closeDropdown();
  editor.focus();
}

function openDropdown() {
  renderDropdown(searchInput.value);
  dropdown.classList.remove('hidden');
}

function closeDropdown() {
  dropdown.classList.add('hidden');
  highlightedIndex = -1;
}

let highlightedIndex = -1;

function highlightItem(index) {
  const items = dropdown.querySelectorAll('.dropdown-item');
  items.forEach(el => el.classList.remove('highlighted'));
  if (index >= 0 && index < items.length) {
    items[index].classList.add('highlighted');
    items[index].scrollIntoView({ block: 'nearest' });
    highlightedIndex = index;
  }
}

// === Output Display ===

function displayResult(result, elapsed) {
  output.textContent = '';

  if (result.output) {
    output.appendChild(document.createTextNode(result.output));
  }

  if (result.isError && result.error) {
    const span = document.createElement('span');
    span.className = 'error-line';
    span.textContent = (result.output ? '\n' : '') + 'Error: ' + result.error;
    output.appendChild(span);
  }

  if (!result.output && !result.isError) {
    output.innerHTML = '<span class="placeholder">(no output)</span>';
  }

  const ms = elapsed.toFixed(1);
  if (result.isError) {
    setStatus(`Error (${ms}ms)`, 'error');
  } else {
    setStatus(`Done (${ms}ms)`, 'ok');
  }
}

function setStatus(text, type) {
  status.textContent = text;
  status.className = type === 'error' ? 'status-error' : type === 'ok' ? 'status-ok' : '';
}

// === Event Handlers ===

function handleRun() {
  if (!jacl || runBtn.disabled) return;
  const source = editor.value;
  if (!source.trim()) {
    output.innerHTML = '<span class="placeholder">(empty source)</span>';
    setStatus('Ready', 'ok');
    return;
  }

  setStatus('Running...', '');
  // Use requestAnimationFrame to show "Running..." before blocking eval
  requestAnimationFrame(() => {
    requestAnimationFrame(() => {
      try {
        const t0 = performance.now();
        const result = runCode(source);
        const elapsed = performance.now() - t0;
        displayResult(result, elapsed);
      } catch (e) {
        output.textContent = '';
        const span = document.createElement('span');
        span.className = 'error-line';
        span.textContent = 'JS Exception: ' + e.message;
        output.appendChild(span);
        setStatus('Crashed', 'error');
      }
    });
  });
}

// Run button
runBtn.addEventListener('click', handleRun);

// Clear button
clearBtn.addEventListener('click', () => {
  output.innerHTML = '<span class="placeholder">Press Run or Ctrl+Enter to execute</span>';
  setStatus('Ready', 'ok');
});

// Keyboard shortcuts on editor
editor.addEventListener('keydown', (e) => {
  // Ctrl+Enter or Cmd+Enter: run
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    handleRun();
    return;
  }

  // Tab: insert 2 spaces
  if (e.key === 'Tab') {
    e.preventDefault();
    const start = editor.selectionStart;
    const end = editor.selectionEnd;
    editor.value = editor.value.substring(0, start) + '  ' + editor.value.substring(end);
    editor.selectionStart = editor.selectionEnd = start + 2;
  }

  // Prevent Ctrl+S browser save dialog
  if (e.key === 's' && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
  }
});

// Example search input
searchInput.addEventListener('focus', openDropdown);
searchInput.addEventListener('input', () => {
  renderDropdown(searchInput.value);
  dropdown.classList.remove('hidden');
  highlightedIndex = -1;
});

searchInput.addEventListener('keydown', (e) => {
  const items = dropdown.querySelectorAll('.dropdown-item');
  const count = items.length;

  if (e.key === 'ArrowDown') {
    e.preventDefault();
    highlightItem(highlightedIndex < count - 1 ? highlightedIndex + 1 : 0);
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    highlightItem(highlightedIndex > 0 ? highlightedIndex - 1 : count - 1);
  } else if (e.key === 'Enter') {
    e.preventDefault();
    if (highlightedIndex >= 0 && highlightedIndex < count) {
      const name = items[highlightedIndex].dataset.name;
      const ex = examples.find(ex => ex.name === name);
      if (ex) selectExample(ex);
    }
  } else if (e.key === 'Escape') {
    closeDropdown();
    editor.focus();
  }
});

// Close dropdown on outside click
document.addEventListener('click', (e) => {
  if (!e.target.closest('.example-picker')) {
    closeDropdown();
  }
});

// Global Ctrl+Enter
document.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey) && document.activeElement !== editor) {
    e.preventDefault();
    handleRun();
  }
});

// === Init ===
editor.value = DEFAULT_CODE;
Promise.all([initWasm(), loadExamples()]);

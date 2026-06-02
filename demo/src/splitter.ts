/**
 * Drag-to-resize splitter for the playground's two-panel layout.
 *
 * The .panels grid is laid out as `<left-fr> 6px <right-fr>`; we mutate
 * gridTemplateColumns on mousemove to slide the divider. Position is
 * stored as the editor (left) fraction in [MIN_FRAC, 1 - MIN_FRAC] so
 * neither panel can collapse beyond the floor (~15%), and persisted in
 * localStorage so the user's split survives reloads.
 *
 * Double-click on the splitter resets to 50/50.
 */

const SPLIT_KEY = "jacl-playground:split";
const SPLIT_GAP_PX = 6;        // matches CSS .splitter visual width
const MIN_FRAC = 0.15;         // floor for each panel — keeps gutter reachable

function loadSavedFrac(): number {
  const raw = localStorage.getItem(SPLIT_KEY);
  if (!raw) return 0.5;
  const n = Number(raw);
  if (!Number.isFinite(n)) return 0.5;
  return Math.max(MIN_FRAC, Math.min(1 - MIN_FRAC, n));
}

function applyFrac(panels: HTMLElement, frac: number) {
  // Use percent on both sides so the columns track the grid's width
  // rather than baking in the gutter's px contribution.
  const left  = (frac * 100).toFixed(3);
  const right = ((1 - frac) * 100).toFixed(3);
  panels.style.gridTemplateColumns = `${left}% ${SPLIT_GAP_PX}px ${right}%`;
}

export function initSplitter(panels: HTMLElement, splitter: HTMLElement) {
  applyFrac(panels, loadSavedFrac());

  let dragging = false;

  const onDown = (clientX: number) => {
    dragging = true;
    splitter.classList.add("dragging");
    // Block accidental text selection while dragging.
    document.body.style.userSelect = "none";
    document.body.style.cursor = "col-resize";
    void clientX;  // baseline; we recompute from the current rect each move
  };

  const onMove = (clientX: number) => {
    if (!dragging) return;
    const rect = panels.getBoundingClientRect();
    // Subtract half the gutter so the divider sits centered under the cursor.
    const raw = (clientX - rect.left - SPLIT_GAP_PX / 2) / (rect.width - SPLIT_GAP_PX);
    const frac = Math.max(MIN_FRAC, Math.min(1 - MIN_FRAC, raw));
    applyFrac(panels, frac);
    localStorage.setItem(SPLIT_KEY, String(frac));
  };

  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    splitter.classList.remove("dragging");
    document.body.style.userSelect = "";
    document.body.style.cursor = "";
  };

  splitter.addEventListener("mousedown", (e) => {
    e.preventDefault();
    onDown(e.clientX);
  });
  splitter.addEventListener("dblclick", () => {
    applyFrac(panels, 0.5);
    localStorage.setItem(SPLIT_KEY, "0.5");
  });

  // Listen on window so the drag continues even if the cursor briefly
  // leaves the splitter, and so onUp fires no matter where mouseup
  // lands.
  window.addEventListener("mousemove", (e) => onMove(e.clientX));
  window.addEventListener("mouseup", onUp);

  // Basic touch support — same handlers, different events.
  splitter.addEventListener("touchstart", (e) => {
    if (e.touches.length !== 1) return;
    e.preventDefault();
    onDown(e.touches[0].clientX);
  }, { passive: false });
  window.addEventListener("touchmove", (e) => {
    if (!dragging || e.touches.length !== 1) return;
    onMove(e.touches[0].clientX);
  }, { passive: false });
  window.addEventListener("touchend", onUp);
}

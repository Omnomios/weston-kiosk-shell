import * as Compositor from 'Koya/Compositor';
import * as UI from 'Helix/UserInterface';
import * as Event from 'Helix/Event';
import * as Log from 'Helix/Log';
import * as Engine from 'Helix/Engine';

const FONT = '/rom/fonts/test.ttf';

export default async () => {
  const displays = await Compositor.listDisplays();
  const display = displays.length ? displays[0].display : '';
  const common = { display, msaaSamples: 1, keyboardInteractivity: 'none', acceptPointerEvents: false };
  const surfaces = new Map();
  const results = new Map();
  let keyboardExclusive = false;
  let overlayInput;
  let dashboardStatus;
  let queue = Promise.resolve();

  const report = (name, message) => {
    results.set(name, message);
    Log.info('[layer-shell-test] ' + name + ': ' + message);
  };
  const text = async (win, parent, value, size = 16, height = size + 10) => {
    const id = await UI.createElement(win, {
      renderable: { type: 'text', string: value, size, font: FONT, colour: [0.92, 0.95, 1, 1] },
      item: { size: { y: height } }
    });
    await UI.attach(win, parent, id);
    return id;
  };
  const root = async (win) => {
    const id = await UI.createElement(win, {
      layout: { type: 'column', gap: 8, padding: { l: 16, r: 16, t: 12, b: 12 } }
    });
    await UI.attachRoot(win, id);
    return id;
  };
  const status = async () => {
    const lines = Array.from(results, ([name, message]) => name + ': ' + message);
    await UI.setTextString(surfaces.get('xdg'), dashboardStatus, lines.join('\n'));
  };

  const specs = {
    background: { role: 'background', exclusiveZone: -1, namespace: 'koya-test-background' },
    panel: { role: 'bar', edge: 'top', thickness: 52, reserveSpace: true, namespace: 'koya-test-panel' },
    overlay: {
      role: 'overlay', anchor: 'bottom-right', size: { x: 440, y: 200 },
      offset: { x: 24, y: 24 }, exclusiveZone: 0,
      namespace: 'koya-test-overlay', acceptPointerEvents: true
    }
  };
  const colours = {
    background: [0.025, 0.055, 0.09, 1],
    panel: [0.04, 0.25, 0.32, 1],
    overlay: [0.28, 0.10, 0.30, 0.96]
  };

  const createLayer = async (name) => {
    let win;
    try {
      // These roles must remain layer-shell roles: never substitute XDG.
      win = await Compositor.createWindow({ ...common, ...specs[name] });
      surfaces.set(name, win);
      const info = await Compositor.getWindowInfo(win);
      if (info.role !== specs[name].role) {
        throw new Error('Unexpected role ' + info.role + ', expected ' + specs[name].role);
      }
      await Compositor.setClearColor(win, ...colours[name]);
      const content = await root(win);
      if (name === 'background') {
        await text(win, content, 'BACKGROUND LAYER', 24);
        await text(win, content, 'Full output, exclusiveZone = -1, no input');
      } else if (name === 'panel') {
        await text(win, content, 'TOP PANEL  |  52px reserved  |  P: remap panel  O: remap overlay', 16);
      } else {
        await text(win, content, 'OVERLAY LAYER', 20);
        await text(win, content, 'Bottom-right / 24px margins / no reservation', 13);
        overlayInput = await text(win, content, 'Click here; E toggles exclusive keyboard input', 13);
        const marker = await UI.createElement(win, {
          renderable: { type: 'box', colour: [0.3, 0.85, 1, 1], cornerRadius: 6,
            aabb: { min: { x: 0, y: 0 }, max: { x: 24, y: 24 } } },
          item: { size: { x: 24, y: 24 } }
        });
        await UI.attach(win, content, marker);
        const animation = await UI.addAnimation(win, marker, [
          { time: 0, position: { x: 0, y: 0 } },
          { time: 1, position: { x: 320, y: 0 }, ease: 'inOutQuad' },
          { time: 2, position: { x: 0, y: 0 }, ease: 'inOutQuad', looping: true }
        ]);
        await UI.startAnimation(win, marker, animation);
      }
      report(name, 'CREATED ' + info.role + ' ' + info.width + 'x' + info.height);
      Log.info('[layer-shell-test] configure ' + name + ' ' + JSON.stringify(info));
    } catch (error) {
      if (win !== undefined) {
        await Compositor.destroyWindow(win);
        surfaces.delete(name);
      }
      report(name, 'FAILED: ' + String(error.message || error));
      Log.error('[layer-shell-test] Layer creation failed; check zwlr_layer_shell_v1 advertisement/configure support.');
    }
  };

  const dashboard = await Compositor.createWindow({
    ...common, role: 'window', title: 'Koya layer-shell test',
    appId: 'org.koya.weston.layer-shell-test', size: { x: 960, y: 560 },
    acceptPointerEvents: true
  });
  surfaces.set('xdg', dashboard);
  await Compositor.setClearColor(dashboard, 0.07, 0.09, 0.13, 1);
  const content = await root(dashboard);
  await text(dashboard, content, 'Koya / Weston layer-shell test', 26);
  await text(dashboard, content, 'This XDG window shows results; shell elements use real layer-shell roles.', 15);
  await text(dashboard, content, 'P: toggle panel   O: toggle overlay   E: keyboard capture   R: log geometry   Esc: quit', 14);
  await text(dashboard, content, 'Check: panel above XDG; overlay above both; background behind XDG.', 14);
  await text(dashboard, content, 'Resize Weston and verify anchors, 52px panel height, margins and animation.', 14);
  dashboardStatus = await text(dashboard, content, 'Creating layer surfaces...', 16, 180);
  report('xdg', 'CREATED diagnostic window');
  for (const name of ['background', 'panel', 'overlay']) await createLayer(name);
  await status();

  const toggle = async (name) => {
    if (surfaces.has(name)) {
      await Compositor.destroyWindow(surfaces.get(name));
      surfaces.delete(name);
      if (name === 'overlay') keyboardExclusive = false;
      report(name, 'UNMAPPED; press again to recreate');
    } else {
      await createLayer(name);
    }
    await status();
  };
  const handleKey = async ({ key }) => {
    if (key === 1) return Engine.quit(); // Escape (Linux evdev)
    if (key === 25) await toggle('panel'); // P
    if (key === 24) await toggle('overlay'); // O
    if (key === 18 && surfaces.has('overlay')) { // E
      keyboardExclusive = !keyboardExclusive;
      await Compositor.setKeyboardInteractivity(surfaces.get('overlay'), keyboardExclusive ? 'exclusive' : 'none');
      await UI.setTextString(surfaces.get('overlay'), overlayInput,
        keyboardExclusive ? 'EXCLUSIVE keyboard: type here; E releases focus' : 'Keyboard capture released; E enables it');
      report('overlay', 'Keyboard interactivity = ' + (keyboardExclusive ? 'exclusive' : 'none'));
      await status();
    }
    if (key === 19) { // R
      for (const [name, win] of surfaces) {
        const info = await Compositor.getWindowInfo(win);
        Log.info('[layer-shell-test] geometry ' + name + ' ' + JSON.stringify(info));
      }
    }
  };
  Event.on('keyDown', (event) => {
    queue = queue.then(() => handleKey(event)).catch((error) => Log.error(String(error)));
  });
  Event.on('pointerDown', ({ id, button }) => {
    Log.info('[layer-shell-test] pointerDown window=' + id + ' button=' + button);
  });
  Event.on('textInput', ({ id, text: value }) => {
    Log.info('[layer-shell-test] textInput window=' + id + ' text=' + value);
  });
  Log.info('[layer-shell-test] READY. Creation is not a visual conformance PASS; verify stacking, anchors, reservation and input.');
};

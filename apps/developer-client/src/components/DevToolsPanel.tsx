import { useState, useMemo, useCallback } from 'react';
import { DevToolsService, DevResult, ColonistState } from '../services/DevToolsService';
import styles from './DevToolsPanel.module.css';

interface DevToolsPanelProps {
  serverUrl: string;
}

// Authoritative spawnable defNames (assets/world/**). Datalist suggestions, not a hard
// constraint -- any defName can be typed; the server rejects unknown ones with a toast.
const FLORA = [
  'Flora_TreeOak', 'Flora_TreeMaple', 'Flora_TreePalm',
  'Flora_BerryBush', 'Flora_WoodyBush', 'Flora_GrassBlade', 'Flora_Reed',
];
const PLACEABLES = [...FLORA, 'CraftingSpot', 'BasicBox', 'BasicShelf'];
const MATERIALS = ['Wood', 'Stick', 'SmallStone', 'Berry', 'PlantFiber', 'Misc_BioPile'];
const NEEDS = ['Hunger', 'Thirst', 'Energy', 'Bladder', 'Digestion', 'Hygiene', 'Recreation', 'Temperature'];
const GIVE_WHERE = ['site', 'loose', 'colonist', 'storage'];

function DevToolsPanel({ serverUrl }: DevToolsPanelProps) {
  const svc = useMemo(() => new DevToolsService(serverUrl), [serverUrl]);
  const [feedback, setFeedback] = useState<{ ok: boolean; text: string } | null>(null);

  // Spawn
  const [spawnDef, setSpawnDef] = useState('Flora_TreeOak');
  const [spawnAt, setSpawnAt] = useState('0,0');
  const [spawnN, setSpawnN] = useState(1);
  const [spawnScatter, setSpawnScatter] = useState(0);

  // Colonist spawn
  const [colAt, setColAt] = useState('0,0');
  const [colN, setColN] = useState(1);
  const [colName, setColName] = useState('Dev');

  // Give
  const [giveMat, setGiveMat] = useState('Wood');
  const [giveN, setGiveN] = useState(100);
  const [giveWhere, setGiveWhere] = useState('site');
  const [giveAt, setGiveAt] = useState('0,0');

  // Colonist controls
  const [colonists, setColonists] = useState<ColonistState[]>([]);
  const [selected, setSelected] = useState<number | ''>('');
  const [needType, setNeedType] = useState('Hunger');
  const [needValue, setNeedValue] = useState(50);
  const [teleportTo, setTeleportTo] = useState('0,0');

  // Time
  const [timeSet, setTimeSet] = useState('08:00');
  const [timeSkip, setTimeSkip] = useState('2h');

  // Construction
  const [completeId, setCompleteId] = useState('');

  // World-state viewer
  const [stateView, setStateView] = useState('');

  const report = useCallback((label: string, r: DevResult) => {
    const text = r.body?.error ? `${label} -> ${r.body.error}` : `${label} -> ${JSON.stringify(r.body)}`;
    setFeedback({ ok: r.ok && !r.body?.error, text });
  }, []);

  const run = useCallback(
    async (label: string, verb: string, params: Record<string, string | number>) => {
      report(label, await svc.callDev(verb, params));
    },
    [svc, report]
  );

  const refreshColonists = useCallback(async () => {
    const r = await svc.getState('colonists');
    if (r.ok && Array.isArray(r.body?.colonists)) {
      setColonists(r.body.colonists);
      if (r.body.colonists.length && selected === '') {
        setSelected(r.body.colonists[0].id);
      }
    }
    report('state colonists', r);
  }, [svc, report, selected]);

  const showState = useCallback(
    async (what: string) => {
      const r = await svc.getState(what);
      setStateView(JSON.stringify(r.body, null, 2));
      setFeedback({ ok: r.ok && !r.body?.error, text: `state ${what} (${r.status})` });
    },
    [svc]
  );

  const colonistArg: Record<string, string | number> = selected === '' ? {} : { colonist: selected };

  return (
    <div className={styles.panel}>
      <datalist id="placeables">
        {PLACEABLES.map((d) => <option key={d} value={d} />)}
      </datalist>
      <datalist id="materials">
        {MATERIALS.map((d) => <option key={d} value={d} />)}
      </datalist>

      {/* SPAWN */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>Spawn</h3>
        <div className={styles.row}>
          <label className={styles.field}>def
            <input className={styles.input} list="placeables" value={spawnDef}
              onChange={(e) => setSpawnDef(e.target.value)} />
          </label>
          <label className={styles.field}>at (x,y)
            <input className={styles.input} value={spawnAt} onChange={(e) => setSpawnAt(e.target.value)} />
          </label>
          <label className={styles.fieldNarrow}>n
            <input className={styles.input} type="number" min={1} value={spawnN}
              onChange={(e) => setSpawnN(Number(e.target.value))} />
          </label>
          <label className={styles.fieldNarrow}>scatter
            <input className={styles.input} type="number" min={0} value={spawnScatter}
              onChange={(e) => setSpawnScatter(Number(e.target.value))} />
          </label>
          <button className={styles.button}
            onClick={() => run('spawn', 'spawn', { def: spawnDef, at: spawnAt, n: spawnN, scatter: spawnScatter })}>
            Spawn
          </button>
        </div>
        <div className={styles.row}>
          <label className={styles.field}>colonist at (x,y)
            <input className={styles.input} value={colAt} onChange={(e) => setColAt(e.target.value)} />
          </label>
          <label className={styles.fieldNarrow}>n
            <input className={styles.input} type="number" min={1} value={colN}
              onChange={(e) => setColN(Number(e.target.value))} />
          </label>
          <label className={styles.field}>name
            <input className={styles.input} value={colName} onChange={(e) => setColName(e.target.value)} />
          </label>
          <button className={styles.button}
            onClick={() => run('colonist', 'colonist', { at: colAt, n: colN, name: colName })}>
            Spawn colonist
          </button>
        </div>
      </section>

      {/* RESOURCES */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>Resources (give)</h3>
        <div className={styles.row}>
          <label className={styles.field}>material
            <input className={styles.input} list="materials" value={giveMat}
              onChange={(e) => setGiveMat(e.target.value)} />
          </label>
          <label className={styles.fieldNarrow}>n
            <input className={styles.input} type="number" min={1} value={giveN}
              onChange={(e) => setGiveN(Number(e.target.value))} />
          </label>
          <label className={styles.field}>where
            <select className={styles.input} value={giveWhere} onChange={(e) => setGiveWhere(e.target.value)}>
              {GIVE_WHERE.map((w) => <option key={w} value={w}>{w}</option>)}
            </select>
          </label>
          <label className={styles.field}>at (x,y)
            <input className={styles.input} value={giveAt} onChange={(e) => setGiveAt(e.target.value)} />
          </label>
          <button className={styles.button}
            onClick={() => run('give', 'give', { material: giveMat, n: giveN, where: giveWhere, at: giveAt })}>
            Give
          </button>
        </div>
      </section>

      {/* COLONIST */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>Colonist</h3>
        <div className={styles.row}>
          <label className={styles.fieldWide}>target
            <select className={styles.input} value={selected}
              onChange={(e) => setSelected(e.target.value === '' ? '' : Number(e.target.value))}>
              <option value="">-- pick a colonist --</option>
              {colonists.map((c) => (
                <option key={c.id} value={c.id}>
                  {c.name} (#{c.id}) @ {c.x.toFixed(1)},{c.y.toFixed(1)}
                </option>
              ))}
            </select>
          </label>
          <button className={styles.buttonGhost} onClick={refreshColonists}>Refresh</button>
          <button className={styles.button} disabled={selected === ''}
            onClick={() => run('select', 'select', colonistArg)}>Select</button>
          <button className={styles.buttonDanger} disabled={selected === ''}
            onClick={() => run('kill', 'kill', colonistArg)}>Kill</button>
        </div>
        <div className={styles.row}>
          <label className={styles.field}>need
            <select className={styles.input} value={needType} onChange={(e) => setNeedType(e.target.value)}>
              {NEEDS.map((n) => <option key={n} value={n}>{n}</option>)}
            </select>
          </label>
          <label className={styles.fieldNarrow}>value
            <input className={styles.input} type="number" min={0} max={100} value={needValue}
              onChange={(e) => setNeedValue(Number(e.target.value))} />
          </label>
          <button className={styles.button} disabled={selected === ''}
            onClick={() => run('need', 'need', { ...colonistArg, need: needType, value: needValue })}>
            Set need
          </button>
          <label className={styles.field}>teleport to (x,y)
            <input className={styles.input} value={teleportTo} onChange={(e) => setTeleportTo(e.target.value)} />
          </label>
          <button className={styles.button} disabled={selected === ''}
            onClick={() => run('teleport', 'teleport', { ...colonistArg, to: teleportTo })}>
            Teleport
          </button>
        </div>
      </section>

      {/* TIME */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>Time</h3>
        <div className={styles.row}>
          <div className={styles.buttonGroup}>
            <button className={styles.button} onClick={() => run('time pause', 'time', { speed: 0 })}>Pause</button>
            <button className={styles.button} onClick={() => run('time 1x', 'time', { speed: 1 })}>1x</button>
            <button className={styles.button} onClick={() => run('time 3x', 'time', { speed: 2 })}>3x</button>
            <button className={styles.button} onClick={() => run('time 10x', 'time', { speed: 3 })}>10x</button>
          </div>
          <label className={styles.field}>set (HH:MM)
            <input className={styles.input} value={timeSet} onChange={(e) => setTimeSet(e.target.value)} />
          </label>
          <button className={styles.button} onClick={() => run('time set', 'time', { set: timeSet })}>Set</button>
          <label className={styles.field}>skip (Nh / Nm)
            <input className={styles.input} value={timeSkip} onChange={(e) => setTimeSkip(e.target.value)} />
          </label>
          <button className={styles.button} onClick={() => run('time skip', 'time', { skip: timeSkip })}>Skip</button>
        </div>
      </section>

      {/* CONSTRUCTION */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>Construction</h3>
        <div className={styles.row}>
          <div className={styles.buttonGroup}>
            <button className={styles.button} onClick={() => run('freebuild on', 'freebuild', { on: 1 })}>Free-build ON</button>
            <button className={styles.button} onClick={() => run('freebuild off', 'freebuild', { on: 0 })}>OFF</button>
          </div>
          <label className={styles.field}>complete blueprint id
            <input className={styles.input} value={completeId} onChange={(e) => setCompleteId(e.target.value)} />
          </label>
          <button className={styles.button} disabled={completeId === ''}
            onClick={() => run('complete', 'complete', { id: completeId })}>Complete</button>
        </div>
      </section>

      {/* WORLD STATE */}
      <section className={styles.section}>
        <h3 className={styles.sectionTitle}>World state</h3>
        <div className={styles.row}>
          <div className={styles.buttonGroup}>
            <button className={styles.buttonGhost} onClick={() => showState('summary')}>Summary</button>
            <button className={styles.buttonGhost} onClick={() => showState('colonists')}>Colonists</button>
            <button className={styles.buttonGhost} onClick={() => showState('construction')}>Construction</button>
            <button className={styles.buttonGhost} onClick={() => showState('time')}>Time</button>
          </div>
        </div>
        {stateView && <pre className={styles.stateView}>{stateView}</pre>}
      </section>

      {feedback && (
        <div className={`${styles.feedback} ${feedback.ok ? styles.feedbackOk : styles.feedbackErr}`}>
          {feedback.text}
        </div>
      )}
    </div>
  );
}

export default DevToolsPanel;

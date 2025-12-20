import styles from './FrameBudgetBar.module.css';

interface FrameBudgetBarProps {
  tileRenderMs: number;
  entityRenderMs: number;
  updateMs: number;
  swapBuffersMs: number;
  frameTimeMs: number;
  targetMs?: number; // Default 8.33ms for 120fps
}

function FrameBudgetBar({
  tileRenderMs,
  entityRenderMs,
  updateMs,
  swapBuffersMs,
  frameTimeMs,
  targetMs = 8.33
}: FrameBudgetBarProps) {
  // Calculate "other" time (frame overhead not accounted for elsewhere)
  const measuredMs = tileRenderMs + entityRenderMs + updateMs + swapBuffersMs;
  const otherMs = Math.max(0, frameTimeMs - measuredMs);

  // Calculate percentages of target budget
  const tilePercent = (tileRenderMs / targetMs) * 100;
  const entityPercent = (entityRenderMs / targetMs) * 100;
  const updatePercent = (updateMs / targetMs) * 100;
  const swapPercent = (swapBuffersMs / targetMs) * 100;
  const otherPercent = (otherMs / targetMs) * 100;
  const totalPercent = (frameTimeMs / targetMs) * 100;

  // Determine status
  const isOverBudget = frameTimeMs > targetMs;
  const isWarning = frameTimeMs > targetMs * 0.8;

  // Find the biggest offender
  const components = [
    { name: 'Tiles', ms: tileRenderMs, percent: tilePercent },
    { name: 'Entities', ms: entityRenderMs, percent: entityPercent },
    { name: 'Update', ms: updateMs, percent: updatePercent },
    { name: 'GPU', ms: swapBuffersMs, percent: swapPercent },
    { name: 'Other', ms: otherMs, percent: otherPercent },
  ];
  const biggestOffender = components.reduce((a, b) => a.ms > b.ms ? a : b);

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <span className={styles.title}>Frame Budget</span>
        <span className={`${styles.status} ${isOverBudget ? styles.overBudget : isWarning ? styles.warning : styles.ok}`}>
          {frameTimeMs.toFixed(1)}ms / {targetMs.toFixed(1)}ms ({totalPercent.toFixed(0)}%)
        </span>
      </div>

      <div className={styles.barContainer}>
        <div className={styles.bar}>
          <div
            className={styles.segment}
            style={{ width: `${Math.min(tilePercent, 100)}%`, background: 'var(--color-tiles)' }}
            title={`Tiles: ${tileRenderMs.toFixed(2)}ms`}
          />
          <div
            className={styles.segment}
            style={{ width: `${Math.min(entityPercent, 100)}%`, background: 'var(--color-entities)' }}
            title={`Entities: ${entityRenderMs.toFixed(2)}ms`}
          />
          <div
            className={styles.segment}
            style={{ width: `${Math.min(updatePercent, 100)}%`, background: 'var(--color-update)' }}
            title={`Update: ${updateMs.toFixed(2)}ms`}
          />
          <div
            className={styles.segment}
            style={{ width: `${Math.min(swapPercent, 100)}%`, background: 'var(--color-gpu)' }}
            title={`GPU: ${swapBuffersMs.toFixed(2)}ms`}
          />
          <div
            className={styles.segment}
            style={{ width: `${Math.min(otherPercent, 100)}%`, background: 'var(--color-other)' }}
            title={`Other: ${otherMs.toFixed(2)}ms`}
          />
        </div>
        <div className={styles.budgetLine} style={{ left: '100%' }} />
        {totalPercent > 100 && (
          <div className={styles.overflow} style={{ width: `${Math.min(totalPercent - 100, 50)}%` }} />
        )}
      </div>

      <div className={styles.legend}>
        <span className={styles.legendItem}>
          <span className={styles.legendColor} style={{ background: 'var(--color-tiles)' }} />
          Tiles {tileRenderMs.toFixed(1)}ms
        </span>
        <span className={styles.legendItem}>
          <span className={styles.legendColor} style={{ background: 'var(--color-entities)' }} />
          Entities {entityRenderMs.toFixed(1)}ms
        </span>
        <span className={styles.legendItem}>
          <span className={styles.legendColor} style={{ background: 'var(--color-update)' }} />
          Update {updateMs.toFixed(1)}ms
        </span>
        <span className={styles.legendItem}>
          <span className={styles.legendColor} style={{ background: 'var(--color-gpu)' }} />
          GPU {swapBuffersMs.toFixed(1)}ms
        </span>
        <span className={styles.legendItem}>
          <span className={styles.legendColor} style={{ background: 'var(--color-other)' }} />
          Other {otherMs.toFixed(1)}ms
        </span>
      </div>

      <div className={styles.insight} style={{ visibility: biggestOffender.ms > 1 ? 'visible' : 'hidden' }}>
        Biggest: <strong>{biggestOffender.name}</strong> ({biggestOffender.percent.toFixed(0)}% of budget)
      </div>
    </div>
  );
}

export default FrameBudgetBar;

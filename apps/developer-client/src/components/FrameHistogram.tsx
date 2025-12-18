import styles from './FrameHistogram.module.css';

interface FrameHistogramProps {
  histogram0to8ms: number;
  histogram8to16ms: number;
  histogram16to33ms: number;
  histogram33plusMs: number;
  histogramTotal: number;
  frameTime1PercentLow: number;
  spikeCount16ms: number;
  spikeCount33ms: number;
}

const FrameHistogram: React.FC<FrameHistogramProps> = ({
  histogram0to8ms,
  histogram8to16ms,
  histogram16to33ms,
  histogram33plusMs,
  histogramTotal,
  frameTime1PercentLow,
  spikeCount16ms,
  spikeCount33ms,
}) => {
  // Calculate percentages
  const total = histogramTotal || 1;
  const pct0to8 = (histogram0to8ms / total) * 100;
  const pct8to16 = (histogram8to16ms / total) * 100;
  const pct16to33 = (histogram16to33ms / total) * 100;
  const pct33plus = (histogram33plusMs / total) * 100;

  // Determine health status
  const hasSpikes = spikeCount33ms > 0;
  const hasWarnings = spikeCount16ms > 2;

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <span className={styles.title}>Frame Distribution</span>
        {hasSpikes && (
          <span className={styles.alert}>
            {spikeCount33ms} bad frame{spikeCount33ms !== 1 ? 's' : ''}
          </span>
        )}
        {!hasSpikes && hasWarnings && (
          <span className={styles.warning}>
            {spikeCount16ms} slow frame{spikeCount16ms !== 1 ? 's' : ''}
          </span>
        )}
      </div>

      <div className={styles.histogram}>
        <div className={styles.bar}>
          <div
            className={`${styles.segment} ${styles.excellent}`}
            style={{ width: `${pct0to8}%` }}
            title={`<8ms: ${histogram0to8ms} frames (${pct0to8.toFixed(1)}%)`}
          />
          <div
            className={`${styles.segment} ${styles.good}`}
            style={{ width: `${pct8to16}%` }}
            title={`8-16ms: ${histogram8to16ms} frames (${pct8to16.toFixed(1)}%)`}
          />
          <div
            className={`${styles.segment} ${styles.acceptable}`}
            style={{ width: `${pct16to33}%` }}
            title={`16-33ms: ${histogram16to33ms} frames (${pct16to33.toFixed(1)}%)`}
          />
          <div
            className={`${styles.segment} ${styles.bad}`}
            style={{ width: `${pct33plus}%` }}
            title={`>33ms: ${histogram33plusMs} frames (${pct33plus.toFixed(1)}%)`}
          />
        </div>
      </div>

      <div className={styles.legend}>
        <span className={styles.legendItem}>
          <span className={`${styles.dot} ${styles.excellent}`} />
          &lt;8ms {pct0to8.toFixed(0)}%
        </span>
        <span className={styles.legendItem}>
          <span className={`${styles.dot} ${styles.good}`} />
          8-16ms {pct8to16.toFixed(0)}%
        </span>
        <span className={styles.legendItem}>
          <span className={`${styles.dot} ${styles.acceptable}`} />
          16-33ms {pct16to33.toFixed(0)}%
        </span>
        <span className={styles.legendItem}>
          <span className={`${styles.dot} ${styles.bad}`} />
          &gt;33ms {pct33plus.toFixed(0)}%
        </span>
      </div>

      <div className={styles.stats}>
        <span>1% Low: <strong>{frameTime1PercentLow.toFixed(1)}ms</strong></span>
        <span>Window: {histogramTotal} frames</span>
      </div>
    </div>
  );
};

export default FrameHistogram;

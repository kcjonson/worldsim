import styles from './Sparkline.module.css';

interface SparklineProps {
  label: string;
  values: number[];
  unit?: string;
  warningThreshold?: number;
  badThreshold?: number;
}

function Sparkline({
  label,
  values,
  unit = '',
  warningThreshold,
  badThreshold
}: SparklineProps) {
  const currentValue = values.length > 0 ? values[values.length - 1] : 0;

  // Determine status based on thresholds
  let status: 'ok' | 'warning' | 'bad' = 'ok';
  if (badThreshold !== undefined && currentValue >= badThreshold) {
    status = 'bad';
  } else if (warningThreshold !== undefined && currentValue >= warningThreshold) {
    status = 'warning';
  }

  // Calculate Y-axis range
  const minValue = values.length > 0 ? Math.min(...values) : 0;
  const maxValue = values.length > 0 ? Math.max(...values) : 100;
  const range = maxValue - minValue || 1;
  const yPadding = range * 0.1;
  const yMin = Math.max(0, minValue - yPadding);
  const yMax = maxValue + yPadding;

  const scaleX = (index: number) => {
    if (values.length <= 1) return 0;
    return index / (values.length - 1);
  };

  const scaleY = (value: number) => {
    return 1 - (value - yMin) / (yMax - yMin);
  };

  const polylinePoints = values
    .map((value, i) => `${scaleX(i)},${scaleY(value)}`)
    .join(' ');

  return (
    <div className={styles.container}>
      <div className={styles.info}>
        <span className={styles.label}>{label}</span>
        <span className={`${styles.value} ${styles[status]}`}>
          {currentValue.toLocaleString(undefined, { maximumFractionDigits: 1 })}{unit}
        </span>
      </div>
      <svg className={`${styles.svg} ${styles[status]}`} viewBox="0 0 1 1" preserveAspectRatio="none">
        {values.length > 1 && (
          <polyline
            points={polylinePoints}
            fill="none"
            vectorEffect="non-scaling-stroke"
          />
        )}
      </svg>
    </div>
  );
}

export default Sparkline;

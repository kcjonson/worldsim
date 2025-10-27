import styles from './TimeSeriesChart.module.css';

interface TimeSeriesChartProps {
  label: string;
  values: number[];
  unit?: string;
  className?: string;
}

const TimeSeriesChart: React.FC<TimeSeriesChartProps> = ({
  label,
  values,
  unit = '',
  className = ''
}) => {
  // Get current value (last in array)
  const currentValue = values.length > 0 ? values[values.length - 1] : 0;

  // Calculate average value
  const averageValue = values.length > 0
    ? values.reduce((sum, val) => sum + val, 0) / values.length
    : 0;

  // Calculate Y-axis range with padding
  const minValue = values.length > 0 ? Math.min(...values) : 0;
  const maxValue = values.length > 0 ? Math.max(...values) : 100;
  const range = maxValue - minValue || 1;
  const yPadding = range * 0.1;
  const yMin = Math.max(0, minValue - yPadding);
  const yMax = maxValue + yPadding;

  // Scale functions (normalized 0-1)
  const scaleX = (index: number) => {
    if (values.length <= 1) return 0;
    return index / (values.length - 1);
  };

  const scaleY = (value: number) => {
    return 1 - (value - yMin) / (yMax - yMin);
  };

  // Generate polyline points (normalized coordinates, viewBox handles scaling)
  const polylinePoints = values
    .map((value, i) => `${scaleX(i)},${scaleY(value)}`)
    .join(' ');

  return (
    <div className={`${styles.container} ${className}`}>
      <div className={styles.header}>
        <span className={styles.label}>{label}</span>
        <span className={styles.value}>
          {currentValue.toLocaleString(undefined, { maximumFractionDigits: 1 })}{unit}
          <span className={styles.average}>
            {' '}(avg: {averageValue.toLocaleString(undefined, { maximumFractionDigits: 1 })}{unit})
          </span>
        </span>
      </div>
      <svg className={styles.svg} viewBox="0 0 1 1" preserveAspectRatio="none">
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
};

export default TimeSeriesChart;

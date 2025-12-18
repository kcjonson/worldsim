import styles from './StatsRow.module.css';

interface StatItem {
  label: string;
  value: string | number;
  status?: 'ok' | 'warning' | 'bad';
  unit?: string;
}

interface StatsRowProps {
  stats: StatItem[];
}

function StatsRow({ stats }: StatsRowProps) {
  return (
    <div className={styles.container}>
      {stats.map((stat, i) => (
        <div key={i} className={styles.stat}>
          <span className={styles.label}>{stat.label}</span>
          <span className={`${styles.value} ${stat.status ? styles[stat.status] : ''}`}>
            {typeof stat.value === 'number'
              ? stat.value.toLocaleString(undefined, { maximumFractionDigits: 1 })
              : stat.value}
            {stat.unit && <span className={styles.unit}>{stat.unit}</span>}
          </span>
        </div>
      ))}
    </div>
  );
}

export default StatsRow;

import styles from './EcsSystemsBar.module.css';

interface EcsSystemTiming {
  name: string;
  durationMs: number;
}

interface Props {
  systems: EcsSystemTiming[];
  totalUpdateMs: number;
}

// Colors for each system (rotates if more than 7)
const systemColors = [
  '#4fc3f7', // light blue
  '#81c784', // light green
  '#ffb74d', // orange
  '#ba68c8', // purple
  '#f06292', // pink
  '#4db6ac', // teal
  '#ffd54f', // yellow
];

export default function EcsSystemsBar({ systems, totalUpdateMs }: Props) {
  if (systems.length === 0) {
    return null;
  }

  const totalSystems = systems.reduce((sum, s) => sum + s.durationMs, 0);

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <span className={styles.label}>ECS Systems</span>
        <span className={styles.value}>{totalSystems.toFixed(2)}ms</span>
      </div>
      <div className={styles.bar}>
        {systems.map((system, index) => {
          const width = totalUpdateMs > 0 ? (system.durationMs / totalUpdateMs) * 100 : 0;
          return (
            <div
              key={system.name}
              className={styles.segment}
              style={{
                width: `${Math.max(width, 0.5)}%`,
                backgroundColor: systemColors[index % systemColors.length]
              }}
              title={`${system.name}: ${system.durationMs.toFixed(3)}ms`}
            />
          );
        })}
      </div>
      <div className={styles.legend}>
        {systems.map((system, index) => (
          <div key={system.name} className={styles.legendItem}>
            <span
              className={styles.legendColor}
              style={{ backgroundColor: systemColors[index % systemColors.length] }}
            />
            <span className={styles.legendName}>{system.name}</span>
            <span className={styles.legendValue}>{system.durationMs.toFixed(2)}ms</span>
          </div>
        ))}
      </div>
    </div>
  );
}

import { useEffect, useState } from 'react';
import { ServerConnection } from './services/ServerConnection';
import styles from './App.module.css';

const serverUrl = 'http://localhost:8081'; // Or 8080, 8082 for other apps

interface LogEntry {
  level: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR';
  category: string;
  message: string;
  timestamp: number;
  file?: string;
  line?: number;
}

interface MetricsData {
  timestamp: number;
  fps: number;
  frameTimeMs: number;
  frameTimeMinMs: number;
  frameTimeMaxMs: number;
  drawCalls: number;
  vertexCount: number;
  triangleCount: number;
}

function App() {
  const [metrics, setMetrics] = useState<MetricsData>({
    timestamp: 0,
    fps: 0,
    frameTimeMs: 0,
    frameTimeMinMs: 0,
    frameTimeMaxMs: 0,
    drawCalls: 0,
    vertexCount: 0,
    triangleCount: 0
  });
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [connectionStatus, setConnectionStatus] = useState<'connecting' | 'connected' | 'error'>('connecting');

  // Helper to add system log entries
  const addSystemLog = (level: LogEntry['level'], message: string) => {
    const logEntry: LogEntry = {
      level,
      category: 'System',
      message,
      timestamp: Date.now()
    };
    setLogs(prev => [...prev, logEntry].slice(-1000));
  };

  useEffect(() => {
    const connection = new ServerConnection(serverUrl);

    // Connect to metrics stream
    try {
      connection.connect('metrics', {
        endpoint: '/stream/metrics',
        eventType: 'metric',
        handler: (data) => {
          setMetrics(data);
          setConnectionStatus('connected');
        },
        onConnect: () => {
          setConnectionStatus('connected');
          addSystemLog('INFO', `Connected to ${serverUrl}`);
        },
        onDisconnect: () => {
          setConnectionStatus('connecting');
          addSystemLog('WARN', `Disconnected from ${serverUrl} - attempting to reconnect...`);
        }
      });

      // Connect to logs stream
      connection.connect('logs', {
        endpoint: '/stream/logs',
        eventType: 'log',
        handler: (data) => setLogs(prev => [...prev, data].slice(-1000)),
      });
    } catch (error) {
      console.error('Failed to connect:', error);
      setConnectionStatus('error');
      addSystemLog('ERROR', `Failed to connect to ${serverUrl}`);
    }

    return () => connection.disconnectAll();
  }, []);

  return (
    <div className={styles.appContainer}>
      <header className={styles.header}>
        <h1>Developer Client</h1>
        <div className={styles.connectionStatus}>
          <span className={`${styles.statusIndicator} ${styles[connectionStatus]}`} />
          {serverUrl}
        </div>
      </header>

      <div className={styles.content}>
        <div className={styles.metricsPanel}>
          <h2>Metrics</h2>
          <div className={styles.metricsGrid}>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>FPS</div>
              <div className={styles.metricValue}>{metrics.fps.toFixed(1)}</div>
            </div>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>Frame Time</div>
              <div className={styles.metricValue}>{metrics.frameTimeMs.toFixed(2)}ms</div>
            </div>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>Frame Min/Max</div>
              <div className={styles.metricValue}>
                {metrics.frameTimeMinMs.toFixed(2)} / {metrics.frameTimeMaxMs.toFixed(2)}ms
              </div>
            </div>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>Draw Calls</div>
              <div className={styles.metricValue}>{metrics.drawCalls}</div>
            </div>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>Vertices</div>
              <div className={styles.metricValue}>{metrics.vertexCount.toLocaleString()}</div>
            </div>
            <div className={styles.metricCard}>
              <div className={styles.metricLabel}>Triangles</div>
              <div className={styles.metricValue}>{metrics.triangleCount.toLocaleString()}</div>
            </div>
          </div>
        </div>

        <div className={styles.logsPanel}>
          <h2>Logs ({logs.length})</h2>
          <div className={styles.logsContainer}>
            {logs.map((log, i) => {
              const timestamp = new Date(log.timestamp).toLocaleTimeString();
              const location = log.file ? ` (${log.file}:${log.line})` : '';

              return (
                <div key={i} className={`${styles.logEntry} ${styles[`log${log.level}`]}`}>
                  [{timestamp}][{log.category}][{log.level}] {log.message}{location}
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;

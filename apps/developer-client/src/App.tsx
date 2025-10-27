import { useEffect, useState, useRef } from 'react';
import { ServerConnection } from './services/ServerConnection';
import { LocalStorageService, PersistedState } from './services/LocalStorageService';
import { CircularBuffer } from './utils/CircularBuffer';
import TimeSeriesChart from './components/TimeSeriesChart';
import LogViewer from './components/LogViewer';
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

  // Retention settings
  const [metricsRetention, setMetricsRetention] = useState<30 | 60 | 300 | 600>(60);
  const [logsMaxEntries, setLogsMaxEntries] = useState<500 | 1000 | 2000 | 5000>(1000);
  const [logLevelFilter, setLogLevelFilter] = useState<'DEBUG' | 'INFO' | 'WARN' | 'ERROR'>('DEBUG');

  // Circular buffer for metrics history
  const metricsBufferRef = useRef<CircularBuffer<MetricsData> | null>(null);
  const [metricsHistory, setMetricsHistory] = useState<MetricsData[]>([]);

  // Refs for components
  const logViewerRef = useRef<HTMLDivElement>(null);

  // Initialize/recreate metrics buffer when retention changes
  useEffect(() => {
    const samplesPerSecond = 10;
    const capacity = metricsRetention * samplesPerSecond;

    // Get existing history before recreating buffer
    const existingHistory = metricsBufferRef.current?.getAll() || [];

    // Create new buffer with new capacity
    metricsBufferRef.current = new CircularBuffer<MetricsData>(capacity);

    // Restore history into new buffer (will be trimmed if new capacity is smaller)
    existingHistory.forEach(sample => {
      metricsBufferRef.current?.push(sample);
    });

    setMetricsHistory(metricsBufferRef.current.getAll());
  }, [metricsRetention]);

  // Load persisted state on mount
  useEffect(() => {
    const persisted = LocalStorageService.load();
    if (persisted) {
      setMetricsRetention(persisted.metrics.retentionWindow);
      setLogsMaxEntries(persisted.logs.maxEntries);
      setLogLevelFilter(persisted.preferences.logLevelFilter);

      // Restore metrics history
      persisted.metrics.history.forEach(sample => {
        metricsBufferRef.current?.push(sample);
      });
      setMetricsHistory(metricsBufferRef.current?.getAll() || []);

      setLogs(persisted.logs.entries);
    }
  }, []);

  // Add new metrics to buffer
  useEffect(() => {
    if (metrics.timestamp > 0 && metricsBufferRef.current) {
      metricsBufferRef.current.push(metrics);
      setMetricsHistory(metricsBufferRef.current.getAll());
    }
  }, [metrics]);

  // Save state to localStorage on unmount
  useEffect(() => {
    const handleUnload = () => {
      const currentLogs = (logViewerRef.current as any)?.getLogs?.() || logs;

      const state: PersistedState = {
        metrics: {
          history: metricsBufferRef.current?.getAll() || [],
          retentionWindow: metricsRetention
        },
        logs: {
          entries: currentLogs.slice(-logsMaxEntries),
          maxEntries: logsMaxEntries
        },
        preferences: {
          logLevelFilter,
          serverUrl
        }
      };

      LocalStorageService.save(state);
    };

    window.addEventListener('beforeunload', handleUnload);
    return () => {
      handleUnload();
      window.removeEventListener('beforeunload', handleUnload);
    };
  }, [metricsRetention, logsMaxEntries, logLevelFilter, logs]);

  // Helper to add system log entries
  const addSystemLog = (level: LogEntry['level'], message: string) => {
    const logEntry: LogEntry = {
      level,
      category: 'System',
      message,
      timestamp: Date.now()
    };
    setLogs(prev => {
      const updated = [...prev, logEntry];
      return updated.slice(-logsMaxEntries);
    });
  };

  useEffect(() => {
    const connection = new ServerConnection(serverUrl);

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

      connection.connect('logs', {
        endpoint: '/stream/logs',
        eventType: 'log',
        handler: (data) => setLogs(prev => {
          const updated = [...prev, data];
          return updated.slice(-logsMaxEntries);
        }),
      });
    } catch (error) {
      console.error('Failed to connect:', error);
      setConnectionStatus('error');
      addSystemLog('ERROR', `Failed to connect to ${serverUrl}`);
    }

    return () => connection.disconnectAll();
  }, [logsMaxEntries]);

  // Clear history handler
  const handleClearHistory = () => {
    LocalStorageService.clear();
    setLogs([]);
    metricsBufferRef.current?.clear();
    setMetricsHistory([]);
    addSystemLog('INFO', 'History cleared');
  };

  // Extract values for each metric
  const fpsValues = metricsHistory.map(m => m.fps);
  const frameTimeValues = metricsHistory.map(m => m.frameTimeMs);
  const drawCallsValues = metricsHistory.map(m => m.drawCalls);
  const vertexValues = metricsHistory.map(m => m.vertexCount);
  const triangleValues = metricsHistory.map(m => m.triangleCount);

  return (
    <div className={styles.appContainer}>
      <header className={styles.header}>
        <h1>Developer Client</h1>
        <div className={styles.headerControls}>
          <div className={styles.connectionStatus}>
            <span className={`${styles.statusIndicator} ${styles[connectionStatus]}`} />
            {serverUrl}
          </div>
          <button onClick={handleClearHistory} className={styles.clearButton}>
            Clear History
          </button>
        </div>
      </header>

      <div className={styles.content}>
        <div className={styles.metricsPanel}>
          <div className={styles.metricsHeader}>
            <h2>Metrics</h2>
            <select
              value={metricsRetention}
              onChange={e => setMetricsRetention(Number(e.target.value) as 30 | 60 | 300 | 600)}
              className={styles.select}
            >
              <option value={30}>30s</option>
              <option value={60}>1min</option>
              <option value={300}>5min</option>
              <option value={600}>10min</option>
            </select>
          </div>
          <div className={styles.chartsColumn}>
            <TimeSeriesChart
              label="FPS"
              values={fpsValues}
              className="fps"
            />
            <TimeSeriesChart
              label="Frame Time"
              values={frameTimeValues}
              unit="ms"
              className="frameTime"
            />
            <TimeSeriesChart
              label="Draw Calls"
              values={drawCallsValues}
              className="drawCalls"
            />
            <TimeSeriesChart
              label="Vertices"
              values={vertexValues}
              className="vertices"
            />
            <TimeSeriesChart
              label="Triangles"
              values={triangleValues}
              className="triangles"
            />
          </div>
          <div className={styles.statsRow}>
            <span>Min/Max Frame: {metrics.frameTimeMinMs.toFixed(2)} / {metrics.frameTimeMaxMs.toFixed(2)}ms</span>
          </div>
        </div>

        <div className={styles.logsPanel} ref={logViewerRef}>
          <LogViewer
            logs={logs}
            maxEntries={logsMaxEntries}
            onMaxEntriesChange={setLogsMaxEntries}
            onLogsChange={setLogs}
          />
        </div>
      </div>
    </div>
  );
}

export default App;

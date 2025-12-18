import { useEffect, useState, useRef, useCallback } from 'react';
import { ServerConnection } from './services/ServerConnection';
import { LocalStorageService, PersistedState, SceneSession } from './services/LocalStorageService';
import { CircularBuffer } from './utils/CircularBuffer';
import FrameBudgetBar from './components/FrameBudgetBar';
import EcsSystemsBar from './components/EcsSystemsBar';
import FrameHistogram from './components/FrameHistogram';
import StatsRow from './components/StatsRow';
import Sparkline from './components/Sparkline';
import LogViewer from './components/LogViewer';
import { ScenePerformanceLog } from './components/ScenePerformanceLog';
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

interface EcsSystemTiming {
  name: string;
  durationMs: number;
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
  sceneName?: string;
  // Timing breakdown
  tileRenderMs: number;
  entityRenderMs: number;
  updateMs: number;
  tileCount: number;
  entityCount: number;
  visibleChunkCount: number;
  // Histogram
  histogram0to8ms: number;
  histogram8to16ms: number;
  histogram16to33ms: number;
  histogram33plusMs: number;
  histogramTotal: number;
  // Spike detection
  frameTime1PercentLow: number;
  spikeCount16ms: number;
  spikeCount33ms: number;
  // ECS system timings
  ecsSystems: EcsSystemTiming[];
  // GPU timing
  gpuRenderMs: number;
  // System resources
  memoryUsedBytes: number;
  memoryPeakBytes: number;
  cpuUsagePercent: number;
  cpuCoreCount: number;
  inputLatencyMs: number;
  // Main loop timing breakdown
  pollEventsMs: number;
  inputHandleMs: number;
  sceneUpdateMs: number;
  sceneRenderMs: number;
  swapBuffersMs: number;
}

type Tab = 'performance' | 'logs';

function App() {
  const [metrics, setMetrics] = useState<MetricsData>({
    timestamp: 0,
    fps: 0,
    frameTimeMs: 0,
    frameTimeMinMs: 0,
    frameTimeMaxMs: 0,
    drawCalls: 0,
    vertexCount: 0,
    triangleCount: 0,
    tileRenderMs: 0,
    entityRenderMs: 0,
    updateMs: 0,
    tileCount: 0,
    entityCount: 0,
    visibleChunkCount: 0,
    histogram0to8ms: 0,
    histogram8to16ms: 0,
    histogram16to33ms: 0,
    histogram33plusMs: 0,
    histogramTotal: 0,
    frameTime1PercentLow: 0,
    spikeCount16ms: 0,
    spikeCount33ms: 0,
    ecsSystems: [],
    gpuRenderMs: 0,
    memoryUsedBytes: 0,
    memoryPeakBytes: 0,
    cpuUsagePercent: 0,
    cpuCoreCount: 1,
    inputLatencyMs: 0,
    pollEventsMs: 0,
    inputHandleMs: 0,
    sceneUpdateMs: 0,
    sceneRenderMs: 0,
    swapBuffersMs: 0
  });
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [connectionStatus, setConnectionStatus] = useState<'connecting' | 'connected' | 'error'>('connecting');
  const [activeTab, setActiveTab] = useState<Tab>('performance');
  const [isRecording, setIsRecording] = useState(true);
  const [sceneSessions, setSceneSessions] = useState<SceneSession[]>([]);

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

      // Restore metrics history (ensure optional fields have defaults)
      persisted.metrics.history.forEach(sample => {
        metricsBufferRef.current?.push({
          ...sample,
          ecsSystems: sample.ecsSystems || [],
          gpuRenderMs: sample.gpuRenderMs || 0,
          memoryUsedBytes: sample.memoryUsedBytes || 0,
          memoryPeakBytes: sample.memoryPeakBytes || 0,
          cpuUsagePercent: sample.cpuUsagePercent || 0,
          cpuCoreCount: sample.cpuCoreCount || 1,
          inputLatencyMs: sample.inputLatencyMs || 0,
          pollEventsMs: sample.pollEventsMs || 0,
          inputHandleMs: sample.inputHandleMs || 0,
          sceneUpdateMs: sample.sceneUpdateMs || 0,
          sceneRenderMs: sample.sceneRenderMs || 0,
          swapBuffersMs: sample.swapBuffersMs || 0
        });
      });
      setMetricsHistory(metricsBufferRef.current?.getAll() || []);

      setLogs(persisted.logs.entries);

      // Restore scene sessions
      if (persisted.sceneSessions) {
        setSceneSessions(persisted.sceneSessions);
      }
    }
  }, []);

  // Add new metrics to buffer (only when recording)
  useEffect(() => {
    if (metrics.timestamp > 0 && metricsBufferRef.current && isRecording) {
      metricsBufferRef.current.push(metrics);
      setMetricsHistory(metricsBufferRef.current.getAll());
    }
  }, [metrics, isRecording]);

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
        sceneSessions,
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
  }, [metricsRetention, logsMaxEntries, logLevelFilter, logs, sceneSessions]);

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
    setSceneSessions([]);
    addSystemLog('INFO', 'History cleared');
  };

  // Memoized callback for scene sessions change
  const handleSceneSessionsChange = useCallback((sessions: SceneSession[]) => {
    setSceneSessions(sessions);
  }, []);

  // Extract values for each metric (for sparklines)
  const fpsValues = metricsHistory.map(m => m.fps);
  const frameTimeValues = metricsHistory.map(m => m.frameTimeMs);
  const drawCallsValues = metricsHistory.map(m => m.drawCalls);
  const tileRenderValues = metricsHistory.map(m => m.tileRenderMs);
  const entityRenderValues = metricsHistory.map(m => m.entityRenderMs);
  const updateValues = metricsHistory.map(m => m.updateMs);
  const gpuRenderValues = metricsHistory.map(m => m.gpuRenderMs);

  return (
    <div className={styles.appContainer}>
      <header className={styles.header}>
        <h1>Developer Client</h1>
        <div className={styles.headerControls}>
          <div className={styles.connectionStatus}>
            <span className={`${styles.statusIndicator} ${styles[connectionStatus]}`} />
            {serverUrl}
            {metrics.sceneName && <span className={styles.sceneName}>| {metrics.sceneName}</span>}
          </div>
          <button onClick={handleClearHistory} className={styles.clearButton}>
            Clear History
          </button>
        </div>
      </header>

      <nav className={styles.tabNav}>
        <button
          className={`${styles.tabButton} ${activeTab === 'performance' ? styles.tabActive : ''}`}
          onClick={() => setActiveTab('performance')}
        >
          Performance
        </button>
        <button
          className={`${styles.tabButton} ${activeTab === 'logs' ? styles.tabActive : ''}`}
          onClick={() => setActiveTab('logs')}
        >
          Logs
        </button>
      </nav>

      <div className={styles.content}>
        {activeTab === 'performance' && (
          <div className={styles.metricsPanel}>
            <div className={styles.metricsHeader}>
              <div className={styles.metricsControls}>
                <button
                  onClick={() => setIsRecording(!isRecording)}
                  className={`${styles.recordButton} ${isRecording ? styles.recording : styles.paused}`}
                >
                  {isRecording ? '⏸' : '▶'}
                </button>
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
            </div>

            {/* Frame Budget - the hero component */}
            <FrameBudgetBar
              tileRenderMs={metrics.tileRenderMs}
              entityRenderMs={metrics.entityRenderMs}
              updateMs={metrics.updateMs}
              frameTimeMs={metrics.frameTimeMs}
            />

            {/* ECS System Breakdown */}
            {metrics.ecsSystems.length > 0 && (
              <EcsSystemsBar
                systems={metrics.ecsSystems}
                totalUpdateMs={metrics.updateMs}
              />
            )}

            {/* Frame Distribution Histogram */}
            <div className={styles.section}>
              <FrameHistogram
                histogram0to8ms={metrics.histogram0to8ms}
                histogram8to16ms={metrics.histogram8to16ms}
                histogram16to33ms={metrics.histogram16to33ms}
                histogram33plusMs={metrics.histogram33plusMs}
                histogramTotal={metrics.histogramTotal}
                frameTime1PercentLow={metrics.frameTime1PercentLow}
                spikeCount16ms={metrics.spikeCount16ms}
                spikeCount33ms={metrics.spikeCount33ms}
              />
            </div>

            {/* Key Stats */}
            <div className={styles.section}>
              <StatsRow stats={[
                {
                  label: 'FPS',
                  value: metrics.fps,
                  status: metrics.fps >= 55 ? 'ok' : metrics.fps >= 30 ? 'warning' : 'bad'
                },
                {
                  label: 'Frame',
                  value: metrics.frameTimeMs,
                  unit: 'ms',
                  status: metrics.frameTimeMs <= 16.67 ? 'ok' : metrics.frameTimeMs <= 33.33 ? 'warning' : 'bad'
                },
                {
                  label: 'GPU',
                  value: metrics.gpuRenderMs,
                  unit: 'ms',
                  status: metrics.gpuRenderMs <= 10 ? 'ok' : metrics.gpuRenderMs <= 16 ? 'warning' : 'bad'
                },
                { label: 'Chunks', value: metrics.visibleChunkCount },
                { label: 'Tiles', value: metrics.tileCount },
                { label: 'Entities', value: metrics.entityCount },
                { label: 'Draw Calls', value: metrics.drawCalls },
                { label: 'Vertices', value: (metrics.vertexCount / 1000).toFixed(1), unit: 'k' },
              ]} />
            </div>

            {/* System Resources */}
            <div className={styles.section}>
              <h3 className={styles.sectionHeader}>System ({metrics.cpuCoreCount} cores, max {metrics.cpuCoreCount * 100}%)</h3>
              <StatsRow stats={[
                {
                  label: 'Memory',
                  value: (metrics.memoryUsedBytes / (1024 * 1024)).toFixed(0),
                  unit: 'MB',
                  status: metrics.memoryUsedBytes < 500 * 1024 * 1024 ? 'ok' :
                          metrics.memoryUsedBytes < 1000 * 1024 * 1024 ? 'warning' : 'bad'
                },
                {
                  label: 'Peak',
                  value: (metrics.memoryPeakBytes / (1024 * 1024)).toFixed(0),
                  unit: 'MB'
                },
                {
                  label: 'CPU',
                  value: `${metrics.cpuUsagePercent.toFixed(0)}/${metrics.cpuCoreCount * 100}`,
                  unit: '%',
                  status: metrics.cpuUsagePercent < metrics.cpuCoreCount * 25 ? 'ok' :
                          metrics.cpuUsagePercent < metrics.cpuCoreCount * 50 ? 'warning' : 'bad'
                },
              ]} />
            </div>

            {/* Main Loop Timing Breakdown */}
            <div className={styles.section}>
              <h3 className={styles.sectionHeader}>Main Loop Breakdown</h3>
              <StatsRow stats={[
                {
                  label: 'Poll',
                  value: metrics.pollEventsMs.toFixed(2),
                  unit: 'ms',
                  status: metrics.pollEventsMs < 1 ? 'ok' : metrics.pollEventsMs < 5 ? 'warning' : 'bad'
                },
                {
                  label: 'Input',
                  value: metrics.inputHandleMs.toFixed(2),
                  unit: 'ms',
                  status: metrics.inputHandleMs < 2 ? 'ok' : metrics.inputHandleMs < 5 ? 'warning' : 'bad'
                },
                {
                  label: 'Update',
                  value: metrics.sceneUpdateMs.toFixed(2),
                  unit: 'ms',
                  status: metrics.sceneUpdateMs < 4 ? 'ok' : metrics.sceneUpdateMs < 8 ? 'warning' : 'bad'
                },
                {
                  label: 'Render',
                  value: metrics.sceneRenderMs.toFixed(2),
                  unit: 'ms',
                  status: metrics.sceneRenderMs < 8 ? 'ok' : metrics.sceneRenderMs < 16 ? 'warning' : 'bad'
                },
                {
                  label: 'Swap',
                  value: metrics.swapBuffersMs.toFixed(2),
                  unit: 'ms',
                  status: metrics.swapBuffersMs < 2 ? 'ok' : metrics.swapBuffersMs < 10 ? 'warning' : 'bad'
                },
              ]} />
            </div>

            {/* Trends */}
            <div className={styles.section}>
              <h3 className={styles.sectionHeader}>Trends</h3>
              <div className={styles.sparklineGrid}>
                <Sparkline
                  label="FPS"
                  values={fpsValues}
                  warningThreshold={55}
                  badThreshold={30}
                />
                <Sparkline
                  label="Frame"
                  values={frameTimeValues}
                  unit="ms"
                  warningThreshold={16.67}
                  badThreshold={33.33}
                />
                <Sparkline
                  label="Tiles"
                  values={tileRenderValues}
                  unit="ms"
                  warningThreshold={8}
                  badThreshold={12}
                />
                <Sparkline
                  label="Entities"
                  values={entityRenderValues}
                  unit="ms"
                  warningThreshold={4}
                  badThreshold={8}
                />
                <Sparkline
                  label="Update"
                  values={updateValues}
                  unit="ms"
                  warningThreshold={4}
                  badThreshold={8}
                />
                <Sparkline
                  label="GPU"
                  values={gpuRenderValues}
                  unit="ms"
                  warningThreshold={10}
                  badThreshold={16}
                />
                <Sparkline
                  label="Draws"
                  values={drawCallsValues}
                />
              </div>
            </div>

            <ScenePerformanceLog
              currentSceneName={metrics.sceneName}
              fps={metrics.fps}
              frameTimeMs={metrics.frameTimeMs}
              isRecording={isRecording}
              sessions={sceneSessions}
              onSessionsChange={handleSceneSessionsChange}
            />
          </div>
        )}

        {activeTab === 'logs' && (
          <div className={styles.logsPanel} ref={logViewerRef}>
            <LogViewer
              logs={logs}
              maxEntries={logsMaxEntries}
              onMaxEntriesChange={setLogsMaxEntries}
              onLogsChange={setLogs}
            />
          </div>
        )}
      </div>
    </div>
  );
}

export default App;

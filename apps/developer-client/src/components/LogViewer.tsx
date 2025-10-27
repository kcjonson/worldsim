import { useEffect, useRef, useState } from 'react';
import styles from './LogViewer.module.css';

interface LogEntry {
  level: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR';
  category: string;
  message: string;
  timestamp: number;
  file?: string;
  line?: number;
}

interface LogViewerProps {
  logs: LogEntry[];
  maxEntries: 500 | 1000 | 2000 | 5000;
  onMaxEntriesChange: (max: 500 | 1000 | 2000 | 5000) => void;
  onLogsChange: (logs: LogEntry[]) => void;
}

const LogViewer: React.FC<LogViewerProps> = ({
  logs,
  maxEntries,
  onMaxEntriesChange,
  onLogsChange
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const [levelFilter, setLevelFilter] = useState<'DEBUG' | 'INFO' | 'WARN' | 'ERROR'>('DEBUG');
  const [searchText, setSearchText] = useState('');

  // Auto-scroll to bottom when new logs arrive
  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const isScrolledToBottom =
      container.scrollHeight - container.clientHeight <= container.scrollTop + 1;

    if (isScrolledToBottom) {
      container.scrollTop = container.scrollHeight;
    }
  }, [logs]);

  // Trim logs when max entries changes
  useEffect(() => {
    if (logs.length > maxEntries) {
      const trimmed = logs.slice(-maxEntries);
      onLogsChange(trimmed);
    }
  }, [maxEntries]);

  // Filter logs by level and search text
  const shouldShowLog = (log: LogEntry): boolean => {
    const levels = ['DEBUG', 'INFO', 'WARN', 'ERROR'];
    const minLevel = levels.indexOf(levelFilter);
    const logLevel = levels.indexOf(log.level);

    if (logLevel < minLevel) return false;
    if (searchText && !log.message.toLowerCase().includes(searchText.toLowerCase())) {
      return false;
    }

    return true;
  };

  const filteredLogs = logs.filter(shouldShowLog);

  // Get logs for localStorage persistence
  const getLogs = (): LogEntry[] => {
    return logs;
  };

  return (
    <div className={styles.container} ref={(el) => {
      if (el) (el as any).getLogs = getLogs;
    }}>
      <div className={styles.header}>
        <h2 className={styles.title}>Logs ({logs.length})</h2>
        <div className={styles.controls}>
          <select
            value={levelFilter}
            onChange={e => setLevelFilter(e.target.value as LogEntry['level'])}
            className={styles.select}
          >
            <option value="DEBUG">Debug+</option>
            <option value="INFO">Info+</option>
            <option value="WARN">Warning+</option>
            <option value="ERROR">Error</option>
          </select>
          <input
            type="text"
            placeholder="Search..."
            value={searchText}
            onChange={e => setSearchText(e.target.value)}
            className={styles.searchInput}
          />
          <select
            value={maxEntries}
            onChange={e => onMaxEntriesChange(Number(e.target.value) as 500 | 1000 | 2000 | 5000)}
            className={styles.select}
          >
            <option value={500}>500</option>
            <option value={1000}>1000</option>
            <option value={2000}>2000</option>
            <option value={5000}>5000</option>
          </select>
        </div>
      </div>

      <div ref={containerRef} className={styles.logContainer}>
        {filteredLogs.map((log, i) => {
          const timestamp = new Date(log.timestamp).toLocaleTimeString();
          const location = log.file ? ` (${log.file}:${log.line})` : '';

          return (
            <div key={i} className={`${styles.logEntry} ${styles[`log${log.level}`]}`}>
              <span className={styles.timestamp}>[{timestamp}]</span>
              <span className={styles.category}>[{log.category}]</span>
              <span className={styles.level}>[{log.level}]</span>
              <span className={styles.message}>{log.message}</span>
              {location && <span className={styles.location}>{location}</span>}
            </div>
          );
        })}
      </div>
    </div>
  );
};

export default LogViewer;

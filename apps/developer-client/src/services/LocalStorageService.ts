interface MetricSample {
  timestamp: number;
  fps: number;
  frameTimeMs: number;
  frameTimeMinMs: number;
  frameTimeMaxMs: number;
  drawCalls: number;
  vertexCount: number;
  triangleCount: number;
}

interface LogEntry {
  level: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR';
  category: string;
  message: string;
  timestamp: number;
  file?: string;
  line?: number;
}

export interface PersistedState {
  metrics: {
    history: MetricSample[];
    retentionWindow: 30 | 60 | 300 | 600; // seconds
  };
  logs: {
    entries: LogEntry[];
    maxEntries: 500 | 1000 | 2000 | 5000;
  };
  preferences: {
    logLevelFilter: 'DEBUG' | 'INFO' | 'WARN' | 'ERROR';
    serverUrl: string;
  };
}

const STORAGE_KEY = 'developer-client-state';
const MAX_STORAGE_SIZE = 5 * 1024 * 1024; // 5 MB

/**
 * localStorage service with error handling and automatic cleanup.
 */
export class LocalStorageService {
  /**
   * Load persisted state from localStorage.
   * Returns null if not found, disabled, or corrupt.
   */
  static load(): PersistedState | null {
    try {
      const json = localStorage.getItem(STORAGE_KEY);
      if (!json) return null;

      const state = JSON.parse(json) as PersistedState;

      // Trim stale data based on retention windows
      const now = Date.now();
      const retentionMs = state.metrics.retentionWindow * 1000;
      state.metrics.history = state.metrics.history.filter(
        sample => now - sample.timestamp < retentionMs
      );

      // Trim logs to max entries
      if (state.logs.entries.length > state.logs.maxEntries) {
        state.logs.entries = state.logs.entries.slice(-state.logs.maxEntries);
      }

      return state;
    } catch (error) {
      console.warn('[LocalStorage] Failed to load state:', error);
      // Corrupt data - clear and start fresh
      localStorage.removeItem(STORAGE_KEY);
      return null;
    }
  }

  /**
   * Save state to localStorage with automatic cleanup on quota errors.
   */
  static save(state: PersistedState): void {
    try {
      const json = JSON.stringify(state);

      // Check size before saving
      if (json.length > MAX_STORAGE_SIZE) {
        console.warn('[LocalStorage] State exceeds 5 MB, trimming...');
        state = this.trimState(state);
      }

      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch (error) {
      if (error instanceof Error && error.name === 'QuotaExceededError') {
        console.warn('[LocalStorage] Quota exceeded, trimming and retrying...');
        try {
          // Aggressive trim and retry
          const trimmed = this.trimState(state, 0.5);
          localStorage.setItem(STORAGE_KEY, JSON.stringify(trimmed));
        } catch (retryError) {
          console.error('[LocalStorage] Failed to save even after trimming:', retryError);
          // Give up, clear storage
          localStorage.clear();
        }
      } else {
        console.error('[LocalStorage] Failed to save state:', error);
      }
    }
  }

  /**
   * Clear all persisted state.
   */
  static clear(): void {
    try {
      localStorage.removeItem(STORAGE_KEY);
    } catch (error) {
      console.warn('[LocalStorage] Failed to clear:', error);
    }
  }

  /**
   * Check if localStorage is available.
   */
  static isAvailable(): boolean {
    try {
      const test = '__localStorage_test__';
      localStorage.setItem(test, test);
      localStorage.removeItem(test);
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Trim state to reduce size.
   * @param state State to trim
   * @param factor Fraction to keep (0.5 = keep 50%)
   */
  private static trimState(state: PersistedState, factor: number = 0.5): PersistedState {
    const metricsToKeep = Math.floor(state.metrics.history.length * factor);
    const logsToKeep = Math.floor(state.logs.entries.length * factor);

    return {
      ...state,
      metrics: {
        ...state.metrics,
        history: state.metrics.history.slice(-metricsToKeep)
      },
      logs: {
        ...state.logs,
        entries: state.logs.entries.slice(-logsToKeep)
      }
    };
  }
}

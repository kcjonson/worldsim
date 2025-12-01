import { useEffect, useRef } from 'react';
import styles from './ScenePerformanceLog.module.css';

export interface SceneSession {
  sceneName: string;
  startTime: number;
  endTime?: number;
  samples: number;
  fpsSum: number;
  frameTimeSum: number;
}

interface Props {
  currentSceneName?: string;
  fps: number;
  frameTimeMs: number;
  isRecording: boolean;
  sessions: SceneSession[];
  onSessionsChange: (sessions: SceneSession[]) => void;
}

export function ScenePerformanceLog({
  currentSceneName,
  fps,
  frameTimeMs,
  isRecording,
  sessions,
  onSessionsChange,
}: Props) {
  const currentSessionRef = useRef<SceneSession | null>(null);
  const lastSceneRef = useRef<string | undefined>(undefined);

  // Handle scene changes and metric accumulation
  useEffect(() => {
    if (!isRecording || !currentSceneName) return;

    // Scene changed - finalize current session and start new one
    if (lastSceneRef.current !== currentSceneName) {
      // Finalize previous session
      if (currentSessionRef.current && currentSessionRef.current.samples > 0) {
        const finalizedSession = {
          ...currentSessionRef.current,
          endTime: Date.now(),
        };
        onSessionsChange([...sessions, finalizedSession]);
      }

      // Start new session
      currentSessionRef.current = {
        sceneName: currentSceneName,
        startTime: Date.now(),
        samples: 0,
        fpsSum: 0,
        frameTimeSum: 0,
      };
      lastSceneRef.current = currentSceneName;
    }

    // Accumulate metrics for current session
    if (currentSessionRef.current && fps > 0) {
      currentSessionRef.current.samples++;
      currentSessionRef.current.fpsSum += fps;
      currentSessionRef.current.frameTimeSum += frameTimeMs;
    }
  }, [currentSceneName, fps, frameTimeMs, isRecording, sessions, onSessionsChange]);

  // Calculate averages for a session
  const getAverages = (session: SceneSession) => {
    if (session.samples === 0) return { fps: 0, frameTime: 0 };
    return {
      fps: session.fpsSum / session.samples,
      frameTime: session.frameTimeSum / session.samples,
    };
  };

  // Format duration
  const formatDuration = (startTime: number, endTime?: number) => {
    const end = endTime || Date.now();
    const durationSecs = Math.round((end - startTime) / 1000);
    if (durationSecs < 60) return `${durationSecs}s`;
    const mins = Math.floor(durationSecs / 60);
    const secs = durationSecs % 60;
    return `${mins}m ${secs}s`;
  };

  // Get current session for live display
  const currentSession = currentSessionRef.current;
  const currentAverages = currentSession ? getAverages(currentSession) : null;

  // Clear handler
  const handleClear = () => {
    currentSessionRef.current = null;
    lastSceneRef.current = undefined;
    onSessionsChange([]);
  };

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <h3>Scene Performance Log</h3>
        <button onClick={handleClear} className={styles.clearButton}>
          Clear
        </button>
      </div>

      {/* Current scene (live) */}
      {currentSession && isRecording && (
        <div className={styles.currentSession}>
          <div className={styles.sessionHeader}>
            <span className={styles.sceneName}>{currentSession.sceneName}</span>
            <span className={styles.liveIndicator}>LIVE</span>
          </div>
          <div className={styles.metrics}>
            <span>FPS: {currentAverages?.fps.toFixed(1) || '-'}</span>
            <span>Frame: {currentAverages?.frameTime.toFixed(2) || '-'}ms</span>
            <span>Samples: {currentSession.samples}</span>
            <span>Duration: {formatDuration(currentSession.startTime)}</span>
          </div>
        </div>
      )}

      {/* Session history */}
      <div className={styles.sessionList}>
        {sessions.slice().reverse().map((session) => {
          const averages = getAverages(session);
          return (
            <div key={`${session.sceneName}-${session.startTime}`} className={styles.session}>
              <div className={styles.sessionHeader}>
                <span className={styles.sceneName}>{session.sceneName}</span>
                <span className={styles.duration}>{formatDuration(session.startTime, session.endTime)}</span>
              </div>
              <div className={styles.metrics}>
                <span>Avg FPS: {averages.fps.toFixed(1)}</span>
                <span>Avg Frame: {averages.frameTime.toFixed(2)}ms</span>
                <span>Samples: {session.samples}</span>
              </div>
            </div>
          );
        })}
        {sessions.length === 0 && !currentSession && (
          <div className={styles.empty}>
            No scene data recorded yet. Navigate between scenes to capture performance metrics.
          </div>
        )}
      </div>
    </div>
  );
}

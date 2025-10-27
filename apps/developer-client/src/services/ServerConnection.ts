type StreamType = 'metrics' | 'logs' | 'ui' | 'hover' | 'events' | 'profiler';

interface StreamConfig {
  endpoint: string;
  eventType: string;
  handler: (data: any) => void;
  onConnect?: () => void;
  onDisconnect?: () => void;
}

export class ServerConnection {
  private serverUrl: string;
  private streams: Map<StreamType, EventSource> = new Map();
  private connectionStates: Map<StreamType, boolean> = new Map();

  constructor(serverUrl: string) {
    this.serverUrl = serverUrl;
  }

  connect(streamType: StreamType, config: StreamConfig): void {
    const url = `${this.serverUrl}${config.endpoint}`;
    const source = new EventSource(url);

    source.addEventListener(config.eventType, (event) => {
      try {
        const data = JSON.parse(event.data);
        config.handler(data);
      } catch (error) {
        console.error(`Failed to parse ${streamType} event:`, error);
      }
    });

    source.addEventListener('open', () => {
      this.connectionStates.set(streamType, true);

      // Notify on connect or reconnect
      if (config.onConnect) {
        config.onConnect();
      }
    });

    source.addEventListener('error', () => {
      const wasConnected = this.connectionStates.get(streamType) !== false;
      this.connectionStates.set(streamType, false);

      // Only notify on first disconnect
      if (wasConnected && config.onDisconnect) {
        config.onDisconnect();
      }
      // EventSource automatically reconnects!
    });

    this.streams.set(streamType, source);
  }

  disconnect(streamType: StreamType): void {
    const source = this.streams.get(streamType);
    if (source) {
      source.close();
      this.streams.delete(streamType);
    }
  }

  disconnectAll(): void {
    this.streams.forEach(source => source.close());
    this.streams.clear();
  }
}

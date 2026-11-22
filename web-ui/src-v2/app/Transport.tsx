import { Button } from '../components/Button';
import { Metadata, Value } from '../components/Typography';

function formatTime(seconds: number) {
  const minutes = Math.floor(seconds / 60);
  const remainder = (seconds - minutes * 60).toFixed(2).padStart(5, '0');
  return `${String(minutes).padStart(2, '0')}:${remainder}`;
}

export function Transport({ durationSeconds, positionSeconds, isPlaying, disabled }:
  { durationSeconds: number; positionSeconds: number; isPlaying: boolean; disabled: boolean }) {
  const progress = durationSeconds > 0 ? Math.min(1, Math.max(0, positionSeconds / durationSeconds)) : 0;
  return <footer className="cs-transport-bar" aria-label="Демонстрационный транспорт">
    <Button tone="transport" className={isPlaying ? 'is-playing' : ''} disabled
      aria-label={isPlaying ? 'Воспроизведение Sample' : 'Sample остановлен'} aria-pressed={isPlaying}
      title="Транспорт отображает состояние audio engine; управление будет подключено отдельным этапом">{isPlaying ? 'Ⅱ' : '▶'}</Button>
    <Button tone="quiet" disabled title="Управление транспортом не входит в read-only этап">STOP</Button>
    <Value className="cs-transport-time">{formatTime(positionSeconds)} <span>/ {formatTime(durationSeconds)}</span></Value>
    <div className="cs-transport-track" aria-label={`Демонстрационная позиция ${Math.round(progress * 100)}%`} role="progressbar"
      aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(progress * 100)}>
      <span style={{ width: `${progress * 100}%` }} />
    </div>
    <Metadata className="cs-transport-master">SAMPLE TRANSPORT</Metadata>
  </footer>;
}

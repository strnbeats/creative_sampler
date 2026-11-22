import { RotaryControl } from '../components/RotaryControl';
import { Label, Metadata, SectionTitle } from '../components/Typography';
import type { ViewSlice } from '../bridge/protocol';

function envelopePath(slice: ViewSlice) {
  const attack = Math.max(0.01, slice.attackSeconds);
  const decay = Math.max(0.01, slice.decaySeconds);
  const release = Math.max(0.01, slice.releaseSeconds);
  const sustainTime = Math.max(0.08, (attack + decay + release) * 0.72);
  const total = attack + decay + sustainTime + release;
  const width = 202;
  const startX = 2;
  const attackX = startX + attack / total * width;
  const decayX = attackX + decay / total * width;
  const releaseX = decayX + sustainTime / total * width;
  const endX = startX + width;
  const sustainY = 6 + (1 - slice.sustain) * 48;
  return `M ${startX} 62 L ${attackX.toFixed(1)} 6 L ${decayX.toFixed(1)} ${sustainY.toFixed(1)} L ${releaseX.toFixed(1)} ${sustainY.toFixed(1)} L ${endX} 62`;
}

export function Inspector({ slice, sliceCount, sampleRate }: { slice: ViewSlice | null; sliceCount: number; sampleRate: number }) {
  if (!slice) return <section className="cs-inspector cs-inspector--empty" aria-label="Slice Inspector">
    <div className="cs-inspector-empty__lead"><Label>02 <span>—</span> FRAGMENT</Label><SectionTitle>No fragment selected</SectionTitle></div>
    <Metadata>CHOOSE A SLICE IN THE SOURCE WAVEFORM</Metadata>
  </section>;

  const duration = slice.endSample - slice.startSample;
  const seconds = sampleRate > 0 ? duration / sampleRate : 0;

  return <section className="cs-inspector" aria-label="Slice Inspector">
    <div className="cs-inspector__identity">
      <Label>02 <span>—</span> SELECTED FRAGMENT</Label>
      <SectionTitle>Slice {String(slice.index).padStart(2, '0')}</SectionTitle>
      <Metadata>{slice.note} <span>·</span> {slice.name}</Metadata>
      <span className="cs-inspector__duration">{seconds.toFixed(2)} s <i /> {sliceCount} fragments</span>
    </div>

    <div className="cs-envelope-panel">
      <div className="cs-inspector-group-title"><Label>AMPLITUDE ENVELOPE</Label><Metadata>ADSR</Metadata></div>
      <svg className="cs-envelope-curve" viewBox="0 0 206 70" role="img" aria-label="Демонстрационная форма ADSR">
        <path className="cs-envelope-curve__guide" d="M2 62H204M2 6V62" />
        <path className="cs-envelope-curve__line" d={envelopePath(slice)} />
      </svg>
      <div className="cs-envelope-values">
        <span>A <b>{slice.attackSeconds < 1 ? `${Math.round(slice.attackSeconds * 1000)} ms` : `${slice.attackSeconds.toFixed(2)} s`}</b></span>
        <span>D <b>{slice.decaySeconds < 1 ? `${Math.round(slice.decaySeconds * 1000)} ms` : `${slice.decaySeconds.toFixed(2)} s`}</b></span>
        <span>S <b>{Math.round(slice.sustain * 100)}%</b></span>
        <span>R <b>{slice.releaseSeconds < 1 ? `${Math.round(slice.releaseSeconds * 1000)} ms` : `${slice.releaseSeconds.toFixed(2)} s`}</b></span>
      </div>
    </div>

    <div className="cs-parameter-panel">
      <div className="cs-inspector-group-title"><Label>SLICE PLAYBACK</Label><Metadata>PER FRAGMENT</Metadata></div>
      <div className="cs-parameter-controls">
        <RotaryControl label="Volume" min={0} max={2} value={slice.volume} accent="mint" readOnly
          formatValue={value => `${Math.round(value * 100)}%`} />
        <RotaryControl label="Pan" min={-1} max={1} value={slice.pan} accent="violet" readOnly
          formatValue={value => Math.abs(value) < 0.01 ? 'CENTER' : `${value < 0 ? 'L' : 'R'} ${Math.round(Math.abs(value) * 100)}`} />
        <RotaryControl label="Pitch" min={-24} max={24} step={1} value={slice.pitch} accent="amber" readOnly
          formatValue={value => `${value > 0 ? '+' : ''}${value} st`} />
      </div>
    </div>
  </section>;
}

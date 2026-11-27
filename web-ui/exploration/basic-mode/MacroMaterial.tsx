import { memo, useId } from 'react';
import type { MacroMaterialId } from './data';

type MaterialProps = { amount: number; paint: (name: string) => string };

const varieties = {
  heat: {
    top: '#7f452d', bottom: '#291419', ink: '#ffc38c', render: Heat,
    outline: 'M26 68C22 53 35 31 52 30C61 29 58 18 76 17C95 15 95 29 107 26C126 22 140 39 136 55C134 69 151 71 146 92C141 111 132 112 126 126C118 143 96 137 82 141C66 147 50 131 45 122C35 108 20 109 23 93C25 84 31 81 26 68Z',
  },
  crush: {
    top: '#40545a', bottom: '#19262f', ink: '#c5e8e2', render: Crush,
    outline: 'M36 31H57V24H88V20H110V29H128V44H140V67H147V86H135V105H127V122H111V137H88V143H58V134H39V116H26V99H21V77H28V53H36Z',
  },
  grain: {
    top: '#99704b', bottom: '#432d23', ink: '#fadcaf', render: Grain,
    outline: 'M26 77C25 56 31 37 49 30C60 25 72 23 79 25C96 26 105 25 116 36C126 47 141 50 143 68C144 91 137 97 126 108C118 117 113 130 102 136C90 142 72 141 58 133C42 123 45 119 34 107C27 98 22 89 26 77Z',
  },
  motion: {
    top: '#387278', bottom: '#172e37', ink: '#c5f5df', render: Motion,
    outline: 'M25 82C29 64 42 63 47 45C51 27 74 19 89 24C104 28 107 33 128 30C141 29 146 43 140 56C133 72 138 77 146 90C154 105 139 121 119 120C100 118 98 137 81 140C55 146 45 119 32 119C20 119 15 101 25 82Z',
  },
  space: {
    top: '#5b4d74', bottom: '#201c37', ink: '#eacde9', render: Space,
    outline: 'M20 103C13 81 39 57 54 48C77 34 96 20 119 29C142 37 155 59 141 80C129 96 117 109 95 121C73 133 33 138 20 103Z',
  },
  chaos: {
    top: '#693a52', bottom: '#2b1a32', ink: '#ffc4ba', render: Chaos,
    outline: 'M25 50L51 33L67 36L86 21L111 26L114 35L138 40L132 57L147 77L137 84L144 103L119 130L102 126L85 143L70 132L42 133L41 117L23 103L31 84L19 69Z',
  },
  air: {
    top: '#b9c8b7', bottom: '#4a7471', ink: '#faffdf', render: Air,
    outline: 'M23 82C25 64 51 61 60 42C69 20 105 15 119 30C132 43 118 57 135 64C155 74 149 105 127 113C102 123 105 144 81 136C59 130 59 113 43 119C21 128 16 100 23 82Z',
  },
  prism: {
    top: '#857082', bottom: '#3e324f', ink: '#fae4cb', render: Prism,
    outline: 'M23 106L28 69L45 58L57 31L73 26L83 20L121 37L120 51L141 62L147 93L132 104L126 128L90 140L58 133L48 120Z',
  },
  echo: {
    top: '#536069', bottom: '#242c38', ink: '#eaccb4', render: Echo,
    outline: 'M27 82C22 67 29 51 45 43L73 24C86 16 112 24 126 40C139 55 145 65 144 83C143 106 126 112 108 121L81 139C66 148 43 130 32 121C24 114 32 99 27 82Z',
  },
  chrome: {
    top: '#8ca9b3', bottom: '#273a47', ink: '#e7fffc', render: Chrome,
    outline: 'M21 79C25 58 49 63 48 45C48 22 72 14 93 28C111 43 108 53 126 56C145 57 155 84 141 103C127 121 109 108 99 124C85 149 56 145 49 124C42 107 14 102 21 79Z',
  },
  energy: {
    top: '#403f73', bottom: '#201738', ink: '#e5e3ff', render: Energy,
    outline: 'M36 52L32 39L56 29L66 33L91 20L113 30L126 48L141 52L138 70L149 94L129 106L125 124L101 131L86 141L70 130L46 132L41 117L21 105L28 87L19 70Z',
  },
  film: {
    top: '#a86a40', bottom: '#453024', ink: '#ffd7a3', render: Film,
    outline: 'M23 55C32 33 54 35 70 37C85 41 91 22 115 27L138 48L132 79C132 94 145 105 134 124C124 143 101 136 87 130C67 124 53 146 32 128L24 104C38 86 20 72 23 55Z',
  },
} as const;

function point(angle: number, radius: number) {
  const radians = angle * Math.PI / 180;
  return [84 + Math.sin(radians) * radius, 80 - Math.cos(radians) * radius] as const;
}

function noise(i: number, salt: number) {
  const n = Math.sin(i * 12.9898 + salt * 78.233) * 43758.5453;
  return n - Math.floor(n);
}

const dust = Array.from({ length: 144 }, (_, i) => ({
  x: 22 + noise(i, 1) * 125, y: 20 + noise(i, 2) * 123,
  r: .35 + noise(i, 3) ** 2 * 2.4, shade: noise(i, 4),
}));

function ribbon(y: number, amplitude: number, phase: number, thickness: number) {
  const line = Array.from({ length: 21 }, (_, i) => {
    const x = 9 + i * 7.5;
    const offset = Math.sin(i * .39 + phase) + .48 * Math.sin(i * .76 - phase);
    return [x, y + offset * amplitude] as const;
  });
  const upper = line.map(([x, position]) => `${x},${position.toFixed(1)}`).join('L');
  const lower = line.reverse().map(([x, position], i) =>
    `${x},${(position + thickness * (1 + .27 * Math.sin(i * .63))).toFixed(1)}`).join('L');
  return `M${upper}L${lower}Z`;
}

function Heat({ amount, paint }: MaterialProps) {
  return <>
    <rect x="20" y="20" width="130" height="120" fill={paint('ember-aura')} opacity={.15 + amount * .83} />
    <g transform="rotate(-27 84 80)">
      {[0, 1, 2, 3, 4].map(i => <g key={i}>
        <path d={ribbon(30 + i * 22, 3 + i * .8 + amount * 6, i * .74, 13 + amount * 8)}
          fill="#28151b" opacity={.94 - amount * .24} />
        <path d={ribbon(42 + i * 22, 3 + i * .8 + amount * 6, i * .74, .8 + amount * (2 + i * .4))}
          fill={paint('ember')} opacity={.48 + .51 * amount} />
      </g>)}
    </g>
    {dust.slice(0, 24).map(({ x, y, r, shade }, i) => <circle key={i} cx={x} cy={y} r={r * .58}
      fill="#ffe2a6" opacity={Math.max(0, amount - shade * .8)} />)}
  </>;
}

const blocks = Array.from({ length: 84 }, (_, i) => ({
  col: i % 12, row: Math.floor(i / 12), seed: noise(i, 5), tone: noise(i, 8),
}));

function Crush({ amount }: MaterialProps) {
  return <>
    <path d="M21 42H104V59H138V72H111V88H142V107H75V128H24Z" fill="#67848a" opacity=".28" />
    {blocks.map(({ col, row, seed, tone }, i) => {
      const intrusion = Math.max(0, amount - seed);
      const shift = (row % 2 ? 1 : -1) * Math.floor(intrusion * 5) * 4;
      const initial = col >= 2 + row / 2 && col <= 9 - row / 4;
      const x = 12 + col * 12 + shift;
      const y = 29 + row * 15;
      const width = Math.max(4, 11 - Math.floor(intrusion * 6));
      return <rect key={i} x={x} y={y} width={width} height={initial ? 11 : 5 + tone * 8}
        fill={tone > .78 ? '#ebb496' : tone > .32 ? '#8bb7b1' : '#4b7283'}
        opacity={initial ? .2 + seed * .62 : intrusion * .85} />;
    })}
    <path d="M27 65H140M23 94H143M30 113H140" stroke="#0e1117" opacity={.21 + .73 * amount}
      strokeWidth={1 + amount * 3} />
  </>;
}

function Grain({ amount, paint }: MaterialProps) {
  return <>
    <rect x="18" y="20" width="134" height="120" fill={paint('grain-field')} opacity={.5 + amount * .22} />
    <path d="M24 115L126 28M36 126L145 30M72 135L145 66" stroke="#f2c07c" strokeWidth=".7" opacity=".23" />
    {dust.map(({ x, y, r, shade }, i) => {
      const presence = Math.min(.88, Math.max(0, amount * 1.12 + .16 - shade) * 2.7);
      return <circle key={i} cx={x} cy={y} r={r * (.66 + amount * .56)}
        fill={i % 4 === 0 ? '#44291d' : i % 3 === 0 ? '#fff1c9' : '#eab677'} opacity={presence} />;
    })}
  </>;
}

function Motion({ amount, paint }: MaterialProps) {
  return <>
    {[0, 1, 2, 3, 4, 5].map(i =>
      <g key={i} opacity={.56 + i * .07}>
        <path d={ribbon(19 + i * 20, 3 + 13 * amount, .62 * i + amount * 1.4, 12 + i * .8)} fill={paint('water')} />
        <path d={ribbon(20 + i * 20, 3 + 13 * amount, .62 * i + amount * 1.4, .9 + amount * 1.4)}
          fill="#c4f2d3" opacity={.52 + amount * .43} />
      </g>)}
  </>;
}

function Space({ amount, paint }: MaterialProps) {
  return <>
    <ellipse cx="98" cy="67" rx={45 + amount * 18} ry={40 + amount * 9} fill={paint('space-light')} opacity={.42 + amount * .54} />
    <g transform="rotate(-29 86 82)">
      {[0, 1, 2, 3, 4].map(i => {
        const depth = 1 - i / 5;
        return <ellipse key={i} cx={82 + i * amount * 5} cy={92 - i * (1 + amount * 4)}
          rx={63 * depth} ry={(24 + amount * 17) * depth}
          fill="none" stroke={i % 2 ? '#e9bfad' : '#b0a3db'}
          strokeOpacity={.22 + amount * (.23 + i * .08)} strokeWidth={1.7 - depth * .65} />;
      })}
    </g>
    {dust.slice(20, 57).map(({ x, y, r, shade }, i) => <circle key={i} cx={x} cy={y} r={r * .48}
      fill="#fce2de" opacity={Math.max(.08, amount - shade * .56)} />)}
  </>;
}

const shards = [
  'M24 51L71 25L59 70L25 94Z', 'M71 25L109 24L97 60L59 70Z',
  'M109 24L140 50L123 79L97 60Z', 'M25 94L59 70L74 97L50 136Z',
  'M59 70L97 60L123 79L74 97Z', 'M123 79L141 56L143 116L112 138Z',
  'M74 97L123 79L112 138L50 136Z',
];

function Chaos({ amount, paint }: MaterialProps) {
  return <>
    <path d="M18 93L75 32L153 99L99 146Z" fill="#e7809a" opacity={.2 + .56 * amount} />
    {shards.map((d, i) => {
      const x = (noise(i, 7) - .5) * 24 * amount;
      const y = (noise(i, 9) - .5) * 18 * amount;
      return <path key={d} d={d} transform={`translate(${x} ${y})`}
        fill={i % 3 === 0 ? paint('fracture') : i % 3 === 1 ? '#632d49' : '#373143'}
        stroke={i % 2 ? '#ed9b91' : '#aa7eac'} strokeWidth={.5 + amount * .95}
        strokeOpacity={.25 + amount * .69} />;
    })}
  </>;
}

function Air({ amount, paint }: MaterialProps) {
  return <>
    <rect x="12" y="16" width="144" height="130" fill="#355f5d" opacity={.52 * (1 - amount)} />
    <ellipse cx="88" cy="75" rx={37 + 29 * amount} ry={26 + 20 * amount} fill={paint('air-light')} />
    <g transform="rotate(-26 84 80)">
      {[0, 1, 2, 3].map(i => {
        const distance = (i - 1.5) * (12 + amount * 17);
        return <g key={i} transform={`translate(${i * amount * 4} ${distance})`}>
          <path d="M16 86Q44 54 85 65T154 50Q118 88 87 86T16 100Z"
            fill={paint('air-film')} opacity={.46 + amount * .2} />
          <path d="M16 86Q44 54 85 65T154 50" fill="none" stroke="#faffee" strokeWidth={.8 + amount * 1.3}
            opacity={.37 + amount * .51} />
        </g>;
      })}
    </g>
  </>;
}

function Prism({ amount, paint }: MaterialProps) {
  return <>
    <path d="M18 110L71 38L117 38L150 95L102 134Z" fill={paint('spectrum')} opacity={.66 + amount * .3} />
    <path d="M71 38L102 134L117 38M18 110L85 79L150 95" fill="none" stroke="#fff4d8" opacity=".5" />
    {['#f5d69f', '#ed9c92', '#d6acd4', '#88c9c2'].map((color, i) =>
      <path key={color} d={`M28 ${85 + i * 2}L81 ${67 + i * 4}L148 ${64 + i * (5 + amount * 10)}`}
        fill="none" stroke={color} strokeWidth={1 + amount * 2.2} opacity={.47 + amount * .44} />)}
  </>;
}

function Echo({ amount, paint }: MaterialProps) {
  return <>
    <path d="M33 116L130 39M33 124L130 47" stroke="#bca3a0" opacity=".25" />
    {[5, 4, 3, 2, 1, 0].map(i => <path key={i}
      transform={`translate(${i * (6 + amount * 8)} ${i * (-3 - amount * 4)})`}
      d="M39 88L55 64H66L79 88L66 111H55Z" fill={i === 0 ? paint('echo-field') : 'none'}
      stroke="#f3cfa7" strokeOpacity={i === 0 ? .88 : Math.max(.08, .63 + amount * .4 - i * .14)}
      strokeWidth={i ? 1.4 + amount : 2} />)}
  </>;
}

function Chrome({ amount, paint }: MaterialProps) {
  return <>
    <rect x="15" y="18" width="138" height="132" fill={paint('chrome')} />
    <g transform={`rotate(${-34 + amount * 20} 84 78)`}>
      <path d="M3 34C55 25 81 101 163 44V69C97 131 47 58 3 62Z" fill="#1b2d42" opacity=".78" />
      <path d="M3 30C55 22 81 101 163 41V55C97 117 47 47 3 49Z" fill={paint('chrome-glint')} />
      <path d="M19 103C62 61 107 162 155 95" fill="none" stroke="#f1f7ec" strokeWidth={3 + 6 * amount} opacity=".84" />
      <path d="M27 116C58 81 102 155 147 106" fill="none" stroke="#405365" strokeWidth="8" opacity=".64" />
    </g>
  </>;
}

function Energy({ amount, paint }: MaterialProps) {
  return <>
    <rect x="18" y="19" width="132" height="125" fill={paint('electric')} />
    {[0, 1, 2, 3, 4, 5].map(i => {
      const peak = 5 + amount * (10 + i * 1.6);
      const y = 50 + i * 13;
      const line = `M19 ${y}H${49 + i * 3}l10 ${-peak} 9 ${peak + 8} 12 -8H116l10 -9 7 9H150`;
      return <g key={i} opacity={.36 + i * .1}>
        <path d={line} fill="none" stroke="#d861e2" strokeWidth={4.3 + amount * 2.5} opacity=".36" />
        <path d={line} fill="none" stroke={i % 2 ? '#fcbec9' : '#c2edfd'} strokeWidth={.94 + amount * .72} />
      </g>;
    })}
  </>;
}

function Film({ amount, paint }: MaterialProps) {
  return <>
    <rect x="19" y="20" width="132" height="122" fill={paint('film')} />
    <g transform={`rotate(${-24 + amount * 12} 84 80)`}>
      {[0, 1, 2, 3].map(i => <g key={i} transform={`translate(${i * 34} 0)`}>
        <rect x="18" y="48" width="29" height="62" rx="4" fill={i % 2 ? '#471f19' : '#713220'} opacity=".55" />
        {[39, 114].map(y => <rect key={y} x="25" y={y} width="13" height="6" rx="2" fill="#ffe4b3" opacity={.46 + amount * .42} />)}
        <path d="M24 81C30 60 37 105 43 72" fill="none" stroke="#ef9c59" strokeWidth={1 + 4 * amount} opacity=".61" />
      </g>)}
    </g>
    <path d="M41 20l34 122m30-112 24 84" stroke="#ffe5b1" opacity={.12 + .22 * amount} strokeWidth=".85" />
  </>;
}

function Relief({ material, amount, ink, tint, paint }: MaterialProps & {
  material: MacroMaterialId; ink: string; tint: string;
}) {
  switch (material) {
    case 'heat':
      return <g stroke="#934b2f" strokeWidth=".8">
        <path d={`M34 64C20 61 16 56 16 49C24 53 25 48 31 41L53 44Z
          M100 35C110 ${25 - amount * 12} 130 12 143 21C132 25 134 36 129 49Z
          M121 120C133 119 147 119 150 125C155 137 141 145 132 134L117 132Z`} fill={paint('ember')} />
        <circle cx="148" cy={42 - amount * 12} r={1.6 + amount * 2.9} fill={ink} />
      </g>;
    case 'crush':
      return <g>
        {[0, 1, 2, 3, 4, 5, 6].map(i => {
          const x = i % 2 ? 133 + (i % 3) * 6 + amount * 7 : 22 - (i % 3) * 4 - amount * 5;
          const y = 34 + i * 13 + (i % 2) * amount * 10;
          return <g key={i} opacity={.52 + .43 * amount}>
            <rect x={x} y={y + 3} width={7 + i % 3 * 2} height="8" fill="#28323d" />
            <rect x={x} y={y} width={7 + i % 3 * 2} height="8" fill={i % 3 ? '#749a9b' : tint} />
          </g>;
        })}
      </g>;
    case 'grain':
      return <g>
        {dust.slice(0, 30).map(({ x, y, r, shade }, i) => {
          const stretch = 1.02 + .23 * amount;
          return <circle key={i} cx={84 + (x - 84) * stretch} cy={80 + (y - 80) * stretch}
            r={r * (.85 + amount * .12)} opacity={(1 - shade) * (.38 + amount * .55)}
            fill={i % 3 === 0 ? ink : '#986546'} />;
        })}
      </g>;
    case 'motion':
      return <g>
        <path d={`M13 113C22 82 46 98 61 78C97 39 113 57 128 40C135 31 ${145 + amount * 8} 28 156 32
          C142 37 147 56 120 72C97 84 82 77 66 102C49 128 29 117 15 127Z`} fill={paint('water')} />
        <path d="M16 111C36 91 46 108 70 77M115 52C139 42 139 31 155 32" stroke={ink} fill="none" opacity=".72" />
      </g>;
    case 'space':
      return <g transform="rotate(-23 85 79)">
        <ellipse cx="85" cy="82" rx={61 + amount * 8} ry={37 + amount * 10} fill={paint('space-light')} opacity=".62" />
        {[0, 1, 2].map(i => <ellipse key={i} cx="85" cy={81 + i * 2} rx={64 + amount * 7 + i * 3}
          ry={36 + amount * 6 + i * 5} fill="none" stroke={i % 2 ? tint : '#9e82ad'} opacity={.48 + amount * .23} />)}
        <circle cx="137" cy="104" r={2.4 + amount * 1.6} fill={ink} />
      </g>;
    case 'chaos':
    case 'prism':
      return <g fill={material === 'chaos' ? paint('fracture') : paint('spectrum')} stroke={ink} strokeOpacity=".7">
        <path d={`M31 69L${12 + amount * 2} 51L34 ${34 - amount * 8}L52 54Z`} />
        <path d={`M107 35L129 ${14 - amount * 5}L146 29L136 55Z`} />
        <path d={`M128 112L${145 + 8 * amount} 104L148 124L128 139Z`} />
        <path d="M32 118L54 130L48 147L30 140Z" />
      </g>;
    case 'air':
      return <g stroke={ink} strokeWidth=".85" strokeOpacity=".88">
        <path d={`M11 79C23 56 58 58 63 37C71 ${16 - amount * 7} 103 ${17 - amount * 8} 131 22
          C109 25 108 52 85 66C58 83 30 66 12 93Z`} fill={paint('air-film')} opacity=".91" />
        <path d={`M67 126C88 114 99 95 125 92Q146 ${91 - amount * 12} 154 78
          C157 105 132 119 117 121C96 126 97 143 69 148Z`} fill={paint('air-film')} />
        <path d="M16 96C26 76 54 78 66 68M107 123C119 112 139 112 147 102" fill="none" />
      </g>;
    case 'echo':
      return <g stroke={ink} strokeWidth="1.35" fill="none">
        {[0, 1, 2].map(i => <path key={i} strokeOpacity={.6 - .13 * i + amount * .1}
          transform={`translate(${0.4 * amount * i},${-i * (4 + amount * 3)})`}
          d={`M18 ${100 + i * 6}Q25 44 90 ${25 - i * 3}Q137 13 ${145 + i * 4} 56`} />)}
        <path d="M42 139C58 152 76 155 93 139" stroke={tint} />
      </g>;
    case 'chrome':
      return <g fill={paint('chrome')} stroke={ink} strokeWidth=".8">
        <path d={`M17 70C11 57 26 42 37 48C45 54 36 63 29 63C24 64 22 74 17 70Z
          M119 46C114 27 129 ${18 - amount * 6} 139 23C157 30 153 49 139 54Z
          M124 130C137 119 150 129 146 139C142 150 125 149 123 140Z`} />
      </g>;
    case 'energy':
      return <g fill="none" strokeLinejoin="round" strokeLinecap="round">
        <path d={`M12 61L28 53L42 34L52 40L64 ${21 - amount * 7}L90 17
          M109 138L121 137L135 119L149 125L156 113`} stroke={tint} strokeWidth={4 + amount * 2} opacity=".48" />
        <path d={`M12 61L28 53L42 34L52 40L64 ${21 - amount * 7}L90 17
          M109 138L121 137L135 119L149 125L156 113`} stroke={ink} strokeWidth="1.45" />
      </g>;
    case 'film':
      return <g>
        <path d={`M14 54C28 29 35 39 60 35L50 48C35 45 24 49 17 71Z
          M103 128C125 131 139 117 147 110C145 ${125 + amount * 9} 130 143 113 150Z`} fill={paint('film')} stroke={ink} />
        <path d="M26 43l5 1m-11 9 6 1m103 83 4-5m7-6 3-5" stroke="#482821" strokeWidth="2.5" />
      </g>;
  }
}

const swatchMarks: Record<MacroMaterialId, string> = {
  heat: 'M34 97Q53 110 77 76T125 59M41 110Q72 128 92 95T131 80',
  crush: 'M41 55h18v18H41ZM80 66h26v16H80ZM56 93h20v17H56ZM98 109h18v12H98Z',
  grain: 'M58 57h1m16 8h1m22-9h1m17 22h1m-58 10h1m17 16h1m21-14h1m5 24h1m-40 7h1',
  motion: 'M32 70Q57 52 82 73T131 67M30 93Q58 75 82 96T131 88',
  space: 'M42 101Q64 60 114 52Q135 52 125 73Q93 119 54 120Q38 119 42 101ZM68 97Q78 73 111 68Q122 70 107 85Q83 106 68 97Z',
  chaos: 'M27 80L60 69L71 108L102 121M60 69L89 45L103 75L131 84M71 108L103 75',
  air: 'M35 83Q78 91 111 48M41 101Q89 113 128 67M55 118Q92 131 127 95',
  prism: 'M40 105L77 47L121 102ZM39 77L77 80L125 66',
  echo: 'M40 85L54 66L67 85L55 104ZM62 79L76 60L89 79L77 98ZM84 73L98 54L111 73L99 92Z',
  chrome: 'M35 71Q57 69 73 88T128 88M40 104Q57 93 75 105T119 110',
  energy: 'M33 78H54l12 -21 10 38 10 -17H132M37 98H59l10 -12 9 24 12 -12H123',
  film: 'M38 67L113 51M41 110L120 95M49 64l9 42m23-49 8 44m21-50 9 44',
};

export function MaterialSwatches({ materials, tint }: {
  materials: readonly MacroMaterialId[]; tint: string;
}) {
  const id = useId();
  return <svg viewBox="0 0 105 54" className="preset-swatches" aria-hidden="true">
    {materials.map((material, index) => {
      const graphic = varieties[material];
      const paintId = `${id}-sample-${index}`;
      return <g key={index} transform={`translate(${index * 29 - 4} ${index % 2 ? 7 : 0}) scale(.3)`}>
        <defs><linearGradient id={paintId} x2=".8" y2="1">
          <stop stopColor={graphic.ink} /><stop offset=".36" stopColor={graphic.top} /><stop offset="1" stopColor={graphic.bottom} />
        </linearGradient></defs>
        <path d={graphic.outline} transform="translate(0 6)" fill={graphic.bottom} />
        <path d={graphic.outline} fill={`url(#${paintId})`} stroke={graphic.ink} strokeWidth="2.2" />
        <path d={graphic.outline} fill={tint} opacity=".25" />
        <path d={swatchMarks[material]} fill="none" stroke={graphic.ink} opacity=".65"
          strokeWidth={material === 'grain' ? 6 : 3} strokeLinecap="round" />
      </g>;
    })}
  </svg>;
}

export const MacroGraphic = memo(function MacroGraphic({ material, tint, value }: {
  material: MacroMaterialId; tint: string; value: number;
}) {
  const id = useId();
  const ref = (name: string) => `url(#${id}-${name})`;
  const amount = value / 100;
  const angle = -128 + amount * 256;
  const { ink, top, bottom, outline, render: Texture } = varieties[material];
  const [tipX, tipY] = point(angle, 76);

  return <svg className="dial-graphic" data-material={material} viewBox="0 0 168 168" aria-hidden="true">
    <defs>
      <clipPath id={`${id}-face`}><path d={outline} /></clipPath>
      <linearGradient id={`${id}-body`} x2="32%" y2="100%">
        <stop stopColor={top} /><stop offset="1" stopColor={bottom} />
      </linearGradient>
      <linearGradient id={`${id}-skirt`} x1="0%" x2="100%">
        <stop stopColor={bottom} /><stop offset=".3" stopColor={top} />
        <stop offset=".8" stopColor={bottom} />
      </linearGradient>
      <radialGradient id={`${id}-shadow`}><stop offset=".4" stopColor="#392719" stopOpacity=".36" />
        <stop offset="1" stopColor="#392719" stopOpacity="0" /></radialGradient>
      <linearGradient id={`${id}-reflection`} x1="14%" y1="14%" x2="82%" y2="83%">
        <stop stopColor="#fff7e4" stopOpacity=".29" />
        <stop offset=".43" stopColor="#fff7e4" stopOpacity=".04" />
        <stop offset=".44" stopColor="#fff7e4" stopOpacity="0" />
        <stop offset="1" stopColor="#14121b" stopOpacity=".18" />
      </linearGradient>
      <linearGradient id={`${id}-chrome`} x1="12%" x2="84%" y1="7%" y2="95%">
        <stop stopColor="#435061" /><stop offset=".2" stopColor="#e6fff9" /><stop offset=".32" stopColor="#8aa4b1" />
        <stop offset=".47" stopColor="#2e3e54" /><stop offset=".59" stopColor="#f8eee0" /><stop offset=".84" stopColor="#527d8d" />
        <stop offset="1" stopColor="#202c39" />
      </linearGradient>
      <linearGradient id={`${id}-chrome-glint`}>
        <stop stopColor="#759aaa" /><stop offset=".52" stopColor="#fcffef" /><stop offset="1" stopColor="#c6e2d5" />
      </linearGradient>
      <radialGradient id={`${id}-electric`}>
        <stop stopColor={tint} /><stop offset=".72" stopColor="#514377" /><stop offset="1" stopColor="#241d3d" />
      </radialGradient>
      <linearGradient id={`${id}-film`} x1="20%" x2="86%" y1="0%" y2="96%">
        <stop stopColor="#edba6d" /><stop offset=".42" stopColor="#925139" /><stop offset="1" stopColor="#3d2926" />
      </linearGradient>
      <linearGradient id={`${id}-ember`}>
        <stop stopColor="#823f2a" />
        <stop offset=".5" stopColor={`hsl(${23 + 17 * amount} 92% ${57 + 30 * amount}%)`} />
        <stop offset=".72" stopColor="#fa8146" /><stop offset="1" stopColor="#ad5032" />
      </linearGradient>
      <radialGradient id={`${id}-ember-aura`}>
        <stop stopColor="#f5b462" /><stop offset=".4" stopColor="#b4472f" />
        <stop offset="1" stopColor="#662b26" stopOpacity=".12" />
      </radialGradient>
      <radialGradient id={`${id}-grain-field`} cx="45%" cy="38%">
        <stop stopColor="#f4c882" /><stop offset=".46" stopColor="#ad784b" />
        <stop offset="1" stopColor="#432c20" />
      </radialGradient>
      <linearGradient id={`${id}-water`} x1="8%" x2="86%" y2="35%">
        <stop stopColor="#34545d" /><stop offset=".37" stopColor="#a4d7c3" />
        <stop offset=".63" stopColor="#479b90" /><stop offset="1" stopColor="#22464c" />
      </linearGradient>
      <radialGradient id={`${id}-space-light`} cx="57%" cy="38%">
        <stop stopColor="#f2c9b6" /><stop offset=".24" stopColor="#b08bbb" />
        <stop offset=".56" stopColor="#645b98" /><stop offset="1" stopColor="#443460" stopOpacity="0" />
      </radialGradient>
      <linearGradient id={`${id}-fracture`} x1="3%" y1="3%" x2="94%" y2="79%">
        <stop stopColor="#eda18f" /><stop offset=".48" stopColor="#a95365" /><stop offset="1" stopColor="#402738" />
      </linearGradient>
      <radialGradient id={`${id}-air-light`}>
        <stop stopColor="#f7f8d5" stopOpacity=".84" />
        <stop offset="1" stopColor="#d3e6de" stopOpacity="0" />
      </radialGradient>
      <linearGradient id={`${id}-air-film`} x1="0%" y1="100%" x2="100%" y2="0%">
        <stop stopColor="#b2d9cc" stopOpacity=".1" /><stop offset=".49" stopColor="#f0f4d9" />
        <stop offset="1" stopColor="#cbdfdc" stopOpacity=".1" />
      </linearGradient>
      <linearGradient id={`${id}-spectrum`} x1="2%" y1="83%" x2="96%" y2="25%">
        <stop stopColor="#424b5d" /><stop offset=".35" stopColor="#8c779c" /><stop offset=".61" stopColor="#d8a38c" />
        <stop offset=".78" stopColor="#f6daaa" /><stop offset="1" stopColor="#77a4a7" />
      </linearGradient>
      <linearGradient id={`${id}-echo-field`} x2="100%" y2="100%">
        <stop stopColor="#c4a685" /><stop offset="1" stopColor="#564339" />
      </linearGradient>
    </defs>

    <ellipse cx="84" cy="110" rx="61" ry="52" fill={ref('shadow')} />
    <g className="dial-body">
      <g className="dial-relief"><Relief material={material} amount={amount} tint={tint} ink={ink} paint={ref} /></g>
      <path d={outline} transform="translate(0 8)" fill={ref('skirt')} stroke={bottom} strokeWidth="1" />
      <path d={outline} transform="translate(0 4)" fill={top} fillOpacity=".8" />
      <path d={outline} fill={ref('body')} />
      <g clipPath={ref('face')}>
        <Texture amount={amount} paint={ref} />
        <path d={outline} fill={tint} opacity=".12" />
        <path d={outline} fill={ref('reflection')} />
        <path d={outline} fill="none" stroke={ink} strokeWidth="6" opacity=".18" />
      </g>
      <path d={outline} fill="none" stroke={bottom} strokeWidth=".75" />
      <path d={outline} transform="translate(0 1)" fill="none" stroke={ink} strokeWidth=".65" opacity=".62" />
    </g>
    {Array.from({ length: 17 }, (_, index) => {
      const [x1, y1] = point(-128 + index * 16, index % 4 === 0 ? 73 : 76);
      const [x2, y2] = point(-128 + index * 16, 80);
      return <line key={index} className={index / 16 <= amount ? 'tick tick-lit' : 'tick'}
        x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth={index % 4 === 0 ? 1.5 : .9} />;
    })}
    <rect className="dial-position" x={tipX - 3.8} y={tipY - 3.8} width="7.6" height="7.6" rx="1"
      transform={`rotate(${angle + 45} ${tipX} ${tipY})`} fill="#a65431" stroke="#f8ebd8" strokeWidth="1.15" />
  </svg>;
});

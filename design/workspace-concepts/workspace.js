(() => {
  'use strict';
  const concepts = [
    {id:'balanced',name:'Баланс',sub:'Слои + свойства',title:'Баланс — всё нужное остаётся на месте',description:'Слои слева, свойства справа, инструменты в узкой полосе. Объект и его параметры видны одновременно.',tradeoff:'Основной вариант для ежедневной вёрстки. На небольшом экране список слоёв можно свернуть.'},
    {id:'unified',name:'Единая панель',sub:'Всё справа',title:'Единая панель — свободная левая половина',description:'Инструменты, короткий список объектов и свойства собраны справа. Большая рабочая зона без левой боковой панели.',tradeoff:'Подходит для простых этикеток. При большом числе объектов правую панель придётся прокручивать.'},
    {id:'bottom',name:'Широкий холст',sub:'Свойства снизу',title:'Широкий холст — вся ширина для макета',description:'Объекты и параметры расположены в нижней полосе. Геометрия, содержимое и выравнивание доступны рядом.',tradeoff:'Удобно для широких этикеток и таблиц. Нижний инспектор забирает часть высоты у вертикальных макетов.'},
    {id:'focus',name:'Фокус',sub:'Панели по запросу',title:'Фокус — этикетка занимает рабочее пространство',description:'Постоянно видны холст и короткая панель действий. Инспектор можно раскрыть, когда понадобятся точные параметры.',tradeoff:'Для вёрстки мышью и небольших экранов. Редкие параметры требуют дополнительного открытия панели.'},
    {id:'ribbon',name:'Контекстная лента',sub:'Параметры сверху',title:'Контекстная лента — короткий путь к частым действиям',description:'Геометрия и свойства выбранного объекта вынесены в одну верхнюю полосу. Справа нет постоянного инспектора.',tradeoff:'Удобно часто менять координаты и выравнивание. Лента отнимает высоту, дополнительные параметры открываются отдельно.'},
    {id:'dual',name:'Макет и результат',sub:'Два вида рядом',title:'Макет и результат — проверка без переключения',description:'Слева редактируемый холст, справа чистый результат; свойства находятся снизу. Выделение не мешает оценке этикетки.',tradeoff:'Для широкого монитора и сложных макетов. На обычном экране каждый из двух видов получается меньше.'}
  ];
  const objects = [
    {id:'title',name:'Название',type:'Текст',symbol:'T',x:44,y:44,w:244,h:27,value:'PRECISION PARTS',font:32},
    {id:'sku',name:'Артикул и партия',type:'Текст',symbol:'T',x:44,y:90,w:226,h:20,value:'SKU: QT-2048 / LOT: 07',font:22},
    {id:'matrix',name:'DataMatrix',type:'2D-код',symbol:'▦',x:44,y:142,w:70,h:70,value:'QT-2048-07',font:5},
    {id:'qty',name:'Количество',type:'Текст',symbol:'T',x:224,y:148,w:95,h:23,value:'QTY 100',font:28},
    {id:'check',name:'Отметка контроля',type:'Текст',symbol:'T',x:224,y:192,w:107,h:20,value:'INSPECTED',font:22},
    {id:'frame',name:'Рамка этикетки',type:'Прямоугольник',symbol:'□',x:20,y:20,w:560,h:280,value:'Толщина линии: 2 dots',font:2}
  ];
  const code = '^XA\n^PW600\n^LL320\n^CI28\n^FO20,20^GB560,280,2^FS\n^FO44,44^A0N,32,0^FDPRECISION PARTS^FS\n^FO44,90^A0N,22,0^FDSKU: QT-2048 / LOT: 07^FS\n^FO44,142^BXN,5,200^FDQT-2048-07^FS\n^FO224,148^A0N,28,0^FDQTY 100^FS\n^FO224,192^A0N,22,0^FDINSPECTED^FS\n^XZ';
  const picture = 'assets/editor-preview.png';
  const app = document.getElementById('application');
  const nav = document.querySelector('.variants');
  let fitObserver;
  const settings = Object.fromEntries(concepts.map(c => [c.id,{selected:'title',grid:true,units:'dots',zoom:'fit',layers:true,inspector:false,view:'design',panel:'object',diagnostics:false,labelDialog:false}]));
  const initial = location.hash.slice(1);
  let current = concepts.some(c=>c.id===initial)?initial:'balanced';
  const state = () => settings[current];
  const selected = () => objects.find(o=>o.id===state().selected);
  const esc = value => String(value).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const ctl = (text,action,extra='',pressed) => `<button type="button" class="control ${extra}" ${action?`data-action="${action}"`: 'disabled'} ${pressed===undefined?'':`aria-pressed="${pressed}"`}>${text}</button>`;
  const field = (name,value,cls='') => `<label class="field ${cls}"><span>${name}</span><input value="${esc(value)}" readonly aria-label="${name}: образец значения"></label>`;
  const format = n => state().units==='mm' ? (n*25.4/203).toFixed(2).replace('.',',') : n;
  const units = () => `<select aria-label="Единицы координат" data-change="units"><option value="dots" ${state().units==='dots'?'selected':''}>dots</option><option value="mm" ${state().units==='mm'?'selected':''}>мм</option></select>`;
  const geometry = () => {const o=selected();return `<div class="field-grid">${field('X',format(o.x))}${field('Y',format(o.y))}${field('Ширина',format(o.w))}${field('Высота',format(o.h))}</div>`};
  const summary = () => `<div class="object-summary"><span class="object-symbol">${selected().symbol}</span><div><strong>${selected().name}</strong><small>${selected().type} · ${objects.indexOf(selected())+1} из 6</small></div></div>`;
  function layers(compact=false){
    return `<div class="layer-list">${objects.map(o=>`<button type="button" class="layer-row" data-select="${o.id}" aria-pressed="${state().selected===o.id}"><span class="layer-type" aria-hidden="true">${o.symbol}</span><span class="layer-name">${o.name}</span>${compact?'':'<small>виден</small>'}</button>`).join('')}</div>`;
  }
  function layersPane(){return `<aside class="pane layers-pane" aria-label="Слои"><div class="pane-heading">Слои <small>6 объектов</small></div>${layers()}<div class="layer-foot">В приложении:<br>Ctrl + клик — несколько<br>F2 — переименовать</div><div class="pane-section"><div class="section-title">Порядок объектов</div><div class="alignment-row">${ctl('Выше',null,'mini')}${ctl('Ниже',null,'mini')}</div></div></aside>`;}
  function palette(){return `<aside class="palette" aria-label="Инструменты"><button type="button" aria-label="Выбор объектов" aria-pressed="true" data-action="design">↖</button><button type="button" aria-label="Добавить текст — элемент образца" disabled>T</button><button type="button" aria-label="Добавить штрихкод — элемент образца" disabled>▥</button><button type="button" aria-label="Добавить DataMatrix — элемент образца" disabled>▦</button><button type="button" aria-label="Добавить фигуру — элемент образца" disabled>□</button><button type="button" aria-label="Добавить таблицу — элемент образца" disabled>▤</button><span class="empty-pad"></span><span class="palette-label">ИНСТРУМЕНТЫ</span></aside>`;}
  function contentFields(){const o=selected();return `<div class="field-grid">${field(o.id==='matrix'?'Данные кода':'Содержимое',o.value,'full')}${field(o.id==='matrix'?'Модуль, dots':o.id==='frame'?'Толщина, dots':'Высота шрифта, dots',o.font)}${field('Поворот','0°')}</div>`;}
  function inspector(withHeader=true){
    if(state().panel==='label')return `<aside class="pane inspector" aria-label="Свойства этикетки">${paneTabs()}<div class="pane-heading">Формат этикетки</div><div class="pane-section"><div class="field-grid">${field('Ширина, dots',600)}${field('Высота, dots',320)}${field('Разрешение', '203 dpi','full')}</div><p class="hint">75,07 × 40,04 мм<br>Геометрия хранится в точках принтера.</p></div><div class="pane-section"><div class="section-title">Рабочие ориентиры</div><label class="checkline"><input type="checkbox" data-change="grid" ${state().grid?'checked':''}> Сетка</label><p class="hint">Сетка и рамки выделения не попадают в печать.</p></div></aside>`;
    return `<aside class="pane inspector" aria-label="Свойства объекта">${withHeader?paneTabs():''}<div class="pane-heading">Выбранный объект <small>свойства</small></div>${summary()}<div class="pane-section"><div class="section-title">Положение и размер ${units()}</div>${geometry()}<label class="checkline"><input type="checkbox" checked disabled> Сохранять пропорции</label></div><div class="pane-section"><div class="section-title">${selected().id==='matrix'?'Параметры кода':'Содержимое'}</div>${contentFields()}</div><div class="pane-section"><div class="section-title">Выравнивание</div><div class="alignment-row">${ctl('Слева',null,'mini')}${ctl('Центр',null,'mini')}${ctl('Справа',null,'mini')}</div></div></aside>`;
  }
  function paneTabs(){return `<div class="pane-tabs"><button type="button" data-action="object-panel" aria-pressed="${state().panel==='object'}">Объект</button><button type="button" data-action="label-panel" aria-pressed="${state().panel==='label'}">Этикетка</button></div>`;}
  function paper(clean=false){
    const o=selected();
    const width=state().zoom==='fit'?'':`style="width:${600*Number(state().zoom)/100}px;max-width:none"`;
    return `<div class="paper-stage" ${width}><img src="${picture}" width="600" height="320" alt="Этикетка PRECISION PARTS: артикул QT-2048, партия 07, DataMatrix, количество 100, отметка INSPECTED" draggable="false">${clean?'':`<div class="ruler-top" aria-hidden="true">${[0,100,200,300,400,500,600].map(n=>`<span>${format(n)}</span>`).join('')}</div><div class="ruler-left" aria-hidden="true">${[0,100,200,300].map(n=>`<span>${format(n)}</span>`).join('')}</div>${[...objects].reverse().map(item=>`<button type="button" class="object-hit ${item.id===o.id?'selected':''}" style="left:${item.x/6}%;top:${item.y/3.2}%;width:${item.w/6}%;height:${item.h/3.2}%" data-select="${item.id}" aria-label="Выбрать: ${item.name}" aria-pressed="${item.id===o.id}"></button>`).join('')}`}<div class="paper-caption">${clean?'Чистый результат · QtZpl':'600 × 320 dots · 75,07 × 40,04 мм'}</div></div>`;
  }
  function canvas(clean=false){
    const isClean=clean||state().view==='preview';
    return `<section class="canvas-column ${clean?'preview-column':''} ${isClean?'preview-clean':''}" aria-label="${clean?'Чистое превью':'Холст этикетки'}"><div class="canvas-top"><span class="canvas-label">${clean?'Чистое превью':state().view==='code'?'Исходный ZPL':isClean?'Превью этикетки':'Холст'}</span><span class="push"></span>${clean?'<span class="small-label">203 dpi · 1 этикетка</span>':`${ctl('Сетка','grid','mini',state().grid)}${ctl('Привязка',null,'mini')} ${current==='focus'?ctl(state().inspector?'Скрыть свойства':'Свойства','inspector','mini',state().inspector):''}`}</div>${!clean&&state().view==='code'?`<div class="source-view"><pre>${esc(code)}</pre></div>`:`<div class="canvas-area ${state().grid&&!isClean?'grid-on':''}">${paper(isClean)}</div>`}<div class="canvas-bottom"><span>${clean?'Без ориентиров и выделения':selected().name}</span><span class="push"></span>${!clean?`<label>Масштаб <select aria-label="Масштаб холста" data-change="zoom"><option value="fit" ${state().zoom==='fit'?'selected':''}>Вписать</option>${[50,75,100,125].map(n=>`<option value="${n}" ${state().zoom===String(n)?'selected':''}>${n}%</option>`).join('')}</select></label>`:'<span>Тот же макет</span>'}</div></section>`;
  }
  function bottomInspector(isDual=false){
    return `<aside class="pane bottom-inspector" aria-label="Нижняя панель свойств"><div class="bottom-group"><div class="section-title">Выбранный объект</div>${summary()}<label class="field"><span>Объекты этикетки</span><select data-change="selected" aria-label="Объекты этикетки">${objects.map(o=>`<option value="${o.id}" ${selected().id===o.id?'selected':''}>${o.name}</option>`).join('')}</select></label></div><div class="bottom-group"><div class="section-title">Содержимое</div>${contentFields()}</div><div class="bottom-group"><div class="section-title">Геометрия ${units()}</div>${geometry()}</div>${isDual?'':`<div class="bottom-group"><div class="section-title">Расположение</div><div class="alignment-row">${ctl('Слева',null,'mini')}${ctl('Центр',null,'mini')}</div><label class="checkline"><input type="checkbox" checked disabled> Сохранять пропорции</label><p class="hint">В приложении:<br>Стрелки — 1 dot<br>Shift + стрелки — 10 dots</p></div>`}</aside>`;
  }
  function ribbon(){return `<section class="ribbon" aria-label="Контекстная лента"><div class="ribbon-group"><div class="section-title">${selected().type.toUpperCase()}</div>${summary()}</div><div class="ribbon-group"><div class="section-title">Положение и размер · ${state().units==='dots'?'dots':'мм'}</div>${geometry()}</div><div class="ribbon-group"><div class="section-title">${selected().id==='matrix'?'Данные':'Содержимое'}</div>${field('Значение',selected().value)}</div><div class="ribbon-group"><div class="section-title">Компоновка</div><div class="alignment-row">${ctl('Слева',null,'mini')}${ctl('Центр',null,'mini')}</div><p class="hint">${ctl('Все свойства','inspector','mini',state().inspector)}</p></div></section>`;}
  function workspace(){
    const collapsed=state().layers?'':' is-collapsed';
    if(current==='balanced')return `<div class="workspace balanced${collapsed}">${palette()}${layersPane()}${canvas()}${inspector()}</div>`;
    if(current==='unified')return `<div class="workspace unified">${canvas()}<aside class="pane inspector" aria-label="Единая панель"><div class="unified-tools">${ctl('+ Текст',null,'mini')}${ctl('+ Код',null,'mini')}${ctl('+ Фигура',null,'mini')}</div><div class="pane-heading">Объекты <small>6</small></div>${layers(true)}${inspector()}</aside></div>`;
    if(current==='bottom')return `<div class="workspace bottom-layout">${palette()}${canvas()}${bottomInspector()}</div>`;
    if(current==='focus')return `<div class="workspace focus-layout ${state().inspector?'has-inspector':''}">${palette()}${canvas()}${state().inspector?`<div class="focus-popover">${inspector()}</div>`:''}<div class="focus-toolstrip"><span class="control flat mini">${selected().name}</span>${ctl('X '+format(selected().x)+' · Y '+format(selected().y),'inspector','mini',state().inspector)}${ctl('Свойства','inspector','mini',state().inspector)}${ctl('Дублировать',null,'mini')}</div></div>`;
    if(current==='ribbon')return `<div class="workspace ribbon-layout${collapsed}">${ribbon()}${layersPane()}${canvas()}${state().inspector?`<div class="focus-popover">${inspector()}</div>`:''}</div>`;
    return `<div class="workspace dual-layout${collapsed}">${layersPane()}${canvas()}${canvas(true)}${bottomInspector(true)}</div>`;
  }
  function labelDialog(){return `<dialog class="format-dialog" aria-labelledby="format-title"><div class="pane-heading"><strong id="format-title">Формат этикетки</strong>${ctl('Закрыть','close-format','mini')}</div><div class="pane-section"><div class="field-grid">${field('Ширина, dots',600)}${field('Высота, dots',320)}${field('Разрешение','203 dpi','full')}</div><p class="hint">75,07 × 40,04 мм · образец формата.<br>Изменение документа в этом макете не выполняется.</p></div></dialog>`;}
  function fitCanvases(){
    app.querySelectorAll('.canvas-area').forEach(area=>{
      const stage=area.querySelector('.paper-stage');if(!stage)return;
      const css=getComputedStyle(area);
      const width=area.clientWidth-parseFloat(css.paddingLeft)-parseFloat(css.paddingRight);
      const height=area.clientHeight-parseFloat(css.paddingTop)-parseFloat(css.paddingBottom);
      const target=state().zoom==='fit'?Math.max(60,Math.min(width,height*600/320)):600*Number(state().zoom)/100;
      stage.style.width=target+'px';stage.style.maxWidth='none';
      area.style.justifyContent=target*320/600>height?'flex-start':'center';
      area.style.alignItems=target>width?'flex-start':'center';
    });
  }
  function render(){
    const active=document.activeElement;
    const focusAttribute=['data-variant','data-action','data-select','data-change'].find(a=>active?.hasAttribute(a));
    const focusSelector=focusAttribute?`[${focusAttribute}="${active.getAttribute(focusAttribute)}"]`:null;
    const focusIndex=focusSelector?[...document.querySelectorAll(focusSelector)].indexOf(active):-1;
    if(fitObserver)fitObserver.disconnect();
    const s=state();const c=concepts.find(c=>c.id===current);
    nav.innerHTML=concepts.map((v,i)=>`<button type="button" class="variant-button" data-variant="${v.id}" aria-pressed="${current===v.id}"><span class="variant-number">0${i+1}</span><span><strong>${v.name}</strong><small>${v.sub}</small></span></button>`).join('');
    document.getElementById('concept-title').textContent=c.title;
    document.getElementById('concept-description').textContent=c.description;
    document.getElementById('concept-tradeoff').textContent=c.tradeoff;
    app.innerHTML=`<div class="app-title"><span class="app-symbol">ZPL</span><strong>QtZpl Designer</strong><span class="docname">product-label.zpl</span><span class="push"></span><span class="concept-badge">КОНЦЕПЦИЯ · ${c.name.toUpperCase()}</span></div><div class="app-commandbar">${ctl('Файл',null,'flat')}${ctl('↶',null,'flat')}${ctl('↷',null,'flat')}<span class="divider"></span><div class="mode-tabs" aria-label="Режим редактора">${[['design','Дизайн'],['code','Код ZPL'],['preview','Превью']].map(([v,l])=>`<button type="button" data-action="${v}" aria-pressed="${s.view===v}">${l}</button>`).join('')}</div><span class="push"></span><span class="doc-format">600 × 320 dots · 203 dpi</span>${['balanced','ribbon','dual'].includes(current)?ctl('Слои','layers','mini',s.layers):''}${ctl('Этикетка','label-panel','mini')}${ctl('Проверка','diagnostics','mini',s.diagnostics)}</div>${workspace()}${s.diagnostics?'<div class="notice" role="status"><strong>Пример этикетки: ошибок QtZpl нет.</strong> 6 объектов · 600 × 320 dots. В макете показано состояние демонстрационного файла; это не проверка физической печати.</div>':''}<div class="statusbar"><span class="status-ok">✓ Пример без ошибок</span><span>6 объектов</span><span>Координаты: ${s.units==='dots'?'dots':'мм'}</span><span class="push"></span><span>Ctrl + колесо — масштаб</span><button type="button" data-action="reset">Сбросить вид</button></div>${s.labelDialog?labelDialog():''}`;
    if(s.labelDialog){const dialog=app.querySelector('dialog');dialog.showModal();dialog.addEventListener('cancel',()=>{s.labelDialog=false;});}
    if(!s.labelDialog&&focusSelector)document.querySelectorAll(focusSelector)[Math.max(0,focusIndex)]?.focus({preventScroll:true});
    fitCanvases();fitObserver=new ResizeObserver(fitCanvases);app.querySelectorAll('.canvas-area').forEach(area=>fitObserver.observe(area));
  }
  function action(name){
    const s=state();
    if(['design','code','preview'].includes(name))s.view=name;
    if(name==='grid')s.grid=!s.grid;
    if(name==='layers')s.layers=!s.layers;
    if(name==='inspector'){s.inspector=!s.inspector;s.panel='object';}
    if(name==='diagnostics')s.diagnostics=!s.diagnostics;
    if(name==='label-panel'){s.panel='label';if(['focus','ribbon'].includes(current))s.inspector=true;if(['bottom','dual'].includes(current))s.labelDialog=true;}
    if(name==='close-format')s.labelDialog=false;
    if(name==='object-panel')s.panel='object';
    if(name==='reset')Object.assign(s,{selected:'title',grid:true,units:'dots',zoom:'fit',layers:true,inspector:false,view:'design',panel:'object',diagnostics:false,labelDialog:false});
    render();
  }
  nav.addEventListener('click',event=>{const btn=event.target.closest('[data-variant]');if(!btn)return;current=btn.dataset.variant;history.replaceState(null,'','#'+current);render();});
  app.addEventListener('click',event=>{const target=event.target.closest('[data-select],[data-action]');if(!target)return;if(target.dataset.select){state().selected=target.dataset.select;state().panel='object';render();}else action(target.dataset.action);});
  app.addEventListener('change',event=>{const key=event.target.dataset.change;if(!key)return;state()[key]=event.target.type==='checkbox'?event.target.checked:event.target.value;render();});
  app.addEventListener('wheel',event=>{if(!event.ctrlKey||!event.target.closest('.canvas-column'))return;event.preventDefault();const steps=['50','75','100','125'];let index=steps.indexOf(state().zoom);if(index<0)index=1;state().zoom=steps[Math.max(0,Math.min(steps.length-1,index+(event.deltaY<0?1:-1)))];render();},{passive:false});
  document.getElementById('theme-toggle').addEventListener('click',event=>{const dark=document.documentElement.dataset.theme!=='dark';document.documentElement.dataset.theme=dark?'dark':'light';event.target.textContent=dark?'Светлая тема':'Тёмная тема';});
  window.addEventListener('hashchange',()=>{const id=location.hash.slice(1);if(concepts.some(c=>c.id===id)){current=id;render();}});
  render();
})();

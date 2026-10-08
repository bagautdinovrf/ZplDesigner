const { chromium } = require('C:/Users/r.bagautdinov/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

(async()=>{
  const browser=await chromium.launch({headless:true});
  const page=await browser.newPage({viewport:{width:1440,height:1050},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const root=__dirname;
  const ids=['balanced','unified','bottom','focus','ribbon','dual'];
  const out=path.join(root,'checks');fs.mkdirSync(out,{recursive:true});
  await page.goto(pathToFileURL(path.join(root,'workspace-designs.html')).href);
  const layout=[];
  for(const width of [1440,1024,768,390,320]){
    await page.setViewportSize({width,height:1050});
    for(const id of ids){
      await page.locator(`[data-variant="${id}"]`).click();
      await page.locator('.paper-stage img').first().evaluate(img=>img.decode());
      const metrics=await page.evaluate(()=>({viewport:innerWidth,width:document.documentElement.scrollWidth,canvas:Math.round(document.querySelector('.canvas-area').getBoundingClientRect().width),image:Math.round(document.querySelector('.paper-stage').getBoundingClientRect().width)}));
      if(metrics.width>width+1)throw Error(`Page overflow: ${id} ${width}: ${JSON.stringify(metrics)}`);
      layout.push({id,...metrics});
      if(width===1440)await page.screenshot({path:path.join(out,id+'.png'),fullPage:true});
      if(width===390&&id==='balanced')await page.screenshot({path:path.join(out,'mobile.png'),fullPage:true});
    }
  }
  await page.setViewportSize({width:1440,height:1050});
  for(const id of ids){
    await page.locator(`[data-variant="${id}"]`).click();
    await page.locator('.object-hit[data-select="matrix"]').first().click();
    if(await page.locator('.object-hit[data-select="matrix"]').first().getAttribute('aria-pressed')!=='true')throw Error('Selection failed '+id);
    if(id==='focus')await page.locator('.canvas-top [data-action="inspector"]').click();
    const unit=page.locator('[data-change="units"]').first();
    if(await unit.count()){
      await unit.selectOption('mm');
      if(!(await page.locator('.statusbar').innerText()).includes('мм'))throw Error('Units failed '+id);
    }
    await page.locator('.canvas-top [data-action="grid"]').first().click();
    if(await page.locator('.grid-on').count())throw Error('Grid failed '+id);
    await page.locator('.mode-tabs [data-action="code"]').click();
    if(!(await page.locator('.source-view').innerText()).includes('^XA'))throw Error('Code failed '+id);
    await page.locator('.mode-tabs [data-action="preview"]').click();
    if(await page.locator('.object-hit:visible').count())throw Error('Preview overlays '+id);
    await page.locator('[data-action="reset"]').click();
    await page.locator('.app-commandbar [data-action="label-panel"]').click();
    if(['bottom','dual'].includes(id)){
      if(!await page.locator('dialog').isVisible())throw Error('Format dialog failed '+id);
      await page.locator('[data-action="close-format"]').click();
    }else if(!await page.locator('[aria-label="Свойства этикетки"]').isVisible())throw Error('Format pane failed '+id);
    await page.locator('[data-action="reset"]').click();
  }
  await page.locator('[data-variant="balanced"]').click();
  await page.locator('#theme-toggle').click();
  await page.screenshot({path:path.join(out,'dark.png'),fullPage:true});
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({errors,layout},null,2));
  if(errors.length)throw Error(errors.join('\n'));
  console.log(JSON.stringify({variants:6,widths:5,interactions:'passed',runtimeErrors:errors.length,screenshots:out}));
  await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});

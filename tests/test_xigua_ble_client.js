const assert=require('node:assert/strict');
const {XiguaBleClient,SERVICE,COMMAND,STATUS}=require('../backend/web/bluetooth.js');
let commands=[],chunks=[],buffer=[],reply={schema:'xigua-ble-wifi-v1',id:0,phase:'ready'},reads=0,stale=false,drop=false;
const listeners={};
const device={name:'Xigua-test',addEventListener:(name,fn)=>listeners[name]=fn,gatt:{connected:false,
  connect:async()=>{device.gatt.connected=true;return {getPrimaryService:async uuid=>{assert.equal(uuid,SERVICE);return service;}};},
  disconnect:()=>{device.gatt.connected=false;listeners.gattserverdisconnected?.();}}};
const service={getCharacteristic:async uuid=>{
  if(uuid===COMMAND)return {writeValueWithResponse:async bytes=>{
    assert(bytes.length<=20);chunks.push(bytes.length);buffer.push(...bytes);
    if(drop){device.gatt.disconnect();return;}
    if(bytes.at(-1)===10){const cmd=JSON.parse(new TextDecoder().decode(new Uint8Array(buffer)));buffer=[];commands.push(cmd);reply={schema:'xigua-ble-wifi-v1',id:cmd.id,phase:'connected',saved:true};reads=0;}
  }};
  assert.equal(uuid,STATUS);return {readValue:async()=>{const r={...reply};if(stale&&++reads===1)r.id=0;return new DataView(new TextEncoder().encode(JSON.stringify(r)).buffer);}};
}};
const env={isSecureContext:true,navigator:{bluetooth:{requestDevice:async filters=>{
  assert.deepEqual(filters,{filters:[{services:[SERVICE]}]});return device;
}}}};
(async()=>{
  assert(XiguaBleClient.capability({isSecureContext:false}).includes('HTTPS'));
  assert(XiguaBleClient.capability({isSecureContext:true,navigator:{}}).includes('安卓 Chrome'));
  let states=[];const client=new XiguaBleClient(state=>states.push(state),env);
  await client.connect();assert.equal(states.at(-1).connected,true);
  stale=true;const result=await client.request('connect',{ssid:'测试 Wi-Fi',password:'abcdefgh'});
  assert.equal(result.id,1);assert.equal(commands[0].ssid,'测试 Wi-Fi');assert.equal(commands[0].password,'abcdefgh');assert(chunks.length>1);
  const pending=client.request('status');await assert.rejects(client.request('scan'),/上一项操作/);await pending;
  await assert.rejects(client.request('connect',{ssid:'x'.repeat(600)}),/太长/);assert.equal(client.busy,false);
  drop=true;await assert.rejects(client.request('scan'),/连接已变化/);assert.equal(client.busy,false);assert.equal(states.at(-1).connected,false);
  buffer=[];drop=false;stale=false;await client.connect();await client.disconnect();assert.equal(commands.at(-1).op,'finish');assert.equal(device.gatt.connected,false);
  const html=require('node:fs').readFileSync(require('node:path').join(__dirname,'../backend/web/console.html'),'utf8');
  const inline=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].at(-1)[1];new Function(inline);
  console.log('BLE client: secure-context/browser checks, real UTF-8 chunks, ACK identity, serialized writes, bounds, disconnect recovery and finish: PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});

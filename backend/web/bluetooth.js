/* Direct phone/device BLE traffic. Credentials are never sent to the cloud. */
(function(root){
  'use strict';
  const SERVICE='7e24a7f0-9b52-4f36-a7f8-84e9d9b00001';
  const COMMAND='7e24a7f0-9b52-4f36-a7f8-84e9d9b00002';
  const STATUS='7e24a7f0-9b52-4f36-a7f8-84e9d9b00003';
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  class XiguaBleClient {
    constructor(onState=()=>{},env=root){this.env=env;this.onState=onState;this.device=null;this.command=null;this.status=null;this.sequence=0;this.busy=false;this.generation=0;}
    static capability(env=root){
      if(!env.isSecureContext)return '请先打开 HTTPS 管理页面，再使用安卓 Chrome 连接蓝牙。';
      if(!env.navigator?.bluetooth)return '此浏览器不支持网页蓝牙，请使用安卓 Chrome。';
      return '';
    }
    reset(){this.generation++;this.command=this.status=null;this.onState({connected:false});}
    async connect(){
      const reason=XiguaBleClient.capability(this.env);if(reason)throw Error(reason);
      if(this.device?.gatt?.connected)this.device.gatt.disconnect();
      const device=await this.env.navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]}]});
      this.device=device;this.generation++;
      device.addEventListener('gattserverdisconnected',()=>{if(this.device===device)this.reset();});
      try{
        const server=await device.gatt.connect(),service=await server.getPrimaryService(SERVICE);
        this.command=await service.getCharacteristic(COMMAND);this.status=await service.getCharacteristic(STATUS);
        // Authenticated characteristic reads prompt Android's six-digit pairing.
        let state;
        for(let attempt=0;attempt<60;attempt++){
          try{state=await this.read();break;}
          catch(error){if(error.name!=='NetworkError'||!device.gatt.connected||attempt===59)throw error;await pause(500);}
        }
        this.onState({...state,connected:true,name:device.name||'西瓜设备'});return state;
      }catch(e){device.gatt.disconnect();this.reset();throw e;}
    }
    async read(){
      if(!this.status||!this.device?.gatt?.connected)throw Error('蓝牙已断开，请重新连接设备。');
      const data=await this.status.readValue(),value=JSON.parse(new TextDecoder().decode(data));
      if(value.schema!=='xigua-ble-wifi-v1')throw Error('设备配网协议不匹配，请更新配套固件。');
      return value;
    }
    async request(op,args={}){
      if(this.busy)throw Error('设备正在处理上一项操作，请稍候。');
      if(!this.command||!this.device?.gatt?.connected)throw Error('请先连接设备。');
      const id=++this.sequence,generation=this.generation;
      const bytes=new TextEncoder().encode(JSON.stringify({...args,op,id})+'\n');
      if(bytes.length>512)throw Error('配置内容太长，请缩短网络名称或密码。');
      this.busy=true;
      try{
        // 20-byte acknowledged chunks also work at the minimum ATT MTU (23).
        for(let offset=0;offset<bytes.length;offset+=20){
          if(generation!==this.generation)throw Error('蓝牙连接已变化，请重试。');
          await this.command.writeValueWithResponse(bytes.slice(offset,offset+20));
        }
        for(let attempt=0;attempt<50;attempt++){
          if(generation!==this.generation)throw Error('蓝牙已断开，请重新连接。');
          const state=await this.read();
          if(state.id===id){this.onState({...state,connected:true,name:this.device.name});return state;}
          await pause(100);
        }
        throw Error('设备响应超时，请重新连接后读取状态。');
      }finally{bytes.fill(0);this.busy=false;}
    }
    async disconnect(){
      try{if(this.device?.gatt?.connected)await this.request('finish');}
      finally{this.device?.gatt?.disconnect();this.reset();}
    }
  }
  root.XiguaBleClient=XiguaBleClient;
  if(typeof module!=='undefined')module.exports={XiguaBleClient,SERVICE,COMMAND,STATUS};
})(typeof window!=='undefined'?window:globalThis);

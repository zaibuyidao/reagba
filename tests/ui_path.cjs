const path=require('node:path');
module.exports=name=>path.join(process.env.REAGBA_UI_DIR||path.resolve(__dirname,'../web'),name);

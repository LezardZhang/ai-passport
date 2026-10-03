package cn.xigua.childcare;

import android.database.Cursor;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.*;
import org.json.*;

/** Streaming private-family backup. The zip is a real archive with a manifest and NDJSON snapshots. */
final class BackupArchive {
    private static final int BUFFER=8192;
    private static final long MAX=512L*1024*1024;
    private static final String[] TABLES={"meta","members","events","outbox","reminders","requests","tracks"};
    static void write(CareApp app,OutputStream target)throws Exception{
        if(target==null)throw new IOException("没有选择备份位置");
        final CountingOutputStream counted=new CountingOutputStream(target,MAX);
        final ZipOutputStream zip=new ZipOutputStream(new BufferedOutputStream(counted));
        JSONObject manifest=new JSONObject();manifest.put("format","xigua-childcare-backup");manifest.put("version",1);manifest.put("schema",3);manifest.put("created_at",System.currentTimeMillis());manifest.put("app_version",app.getPackageManager().getPackageInfo(app.getPackageName(),0).versionName);manifest.put("tables",new JSONArray(TABLES));
        put(zip,"manifest.json",manifest.toString(2).getBytes(StandardCharsets.UTF_8));
        android.database.sqlite.SQLiteDatabase db=app.store.getReadableDatabase();db.beginTransactionNonExclusive();try{
            JSONObject checksums=new JSONObject();for(String table:TABLES)checksums.put("data/"+table+".ndjson",writeTable(app.store,zip,table));put(zip,"checksums.json",checksums.toString(2).getBytes(StandardCharsets.UTF_8));
            db.setTransactionSuccessful();
        }finally{db.endTransaction();}
        JSONObject inventory=new JSONObject();inventory.put("media","audio file paths remain local and are not copied in this version");inventory.put("secrets","excluded");put(zip,"media-inventory.json",inventory.toString(2).getBytes(StandardCharsets.UTF_8));
        zip.finish();zip.close();
    }
    private static String writeTable(Store s,ZipOutputStream zip,String table)throws Exception{
        ZipEntry entry=new ZipEntry("data/"+table+".ndjson");zip.putNextEntry(entry);java.security.MessageDigest digest=java.security.MessageDigest.getInstance("SHA-256");Cursor c=null;try{c=s.getReadableDatabase().rawQuery("SELECT * FROM "+table,null);String[] names=c.getColumnNames();while(c.moveToNext()){JSONObject row=new JSONObject();for(int i=0;i<names.length;i++){int type=c.getType(i);if(type==Cursor.FIELD_TYPE_NULL)row.put(names[i],JSONObject.NULL);else if(type==Cursor.FIELD_TYPE_INTEGER)row.put(names[i],c.getLong(i));else if(type==Cursor.FIELD_TYPE_FLOAT)row.put(names[i],c.getDouble(i));else row.put(names[i],c.getString(i));}byte[] line=(row.toString()+"\n").getBytes(StandardCharsets.UTF_8);zip.write(line);digest.update(line);}}finally{if(c!=null)c.close();zip.closeEntry();}StringBuilder hex=new StringBuilder();for(byte b:digest.digest())hex.append(String.format(java.util.Locale.ROOT,"%02x",b));return hex.toString();}
    private static void put(ZipOutputStream z,String name,byte[] data)throws IOException{if(data.length>1024*1024)throw new IOException("备份元数据过大");z.putNextEntry(new ZipEntry(name));z.write(data);z.closeEntry();}
    private static final class CountingOutputStream extends FilterOutputStream{long count;final long max;CountingOutputStream(OutputStream o,long m){super(o);max=m;}public void write(int b)throws IOException{if(++count>max)throw new IOException("备份超过 512 MiB 上限");out.write(b);}public void write(byte[] b,int off,int len)throws IOException{if(count+len>max)throw new IOException("备份超过 512 MiB 上限");count+=len;out.write(b,off,len);}}
}

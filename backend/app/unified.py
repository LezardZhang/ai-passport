"""Shared administrative session and console; preserve backup client protocols."""
from contextlib import AsyncExitStack, asynccontextmanager
import hmac
import json
import os
from urllib.parse import urlsplit
from pathlib import Path
from html import escape as html_escape

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse
from pydantic import BaseModel


class RestoreProxyPrefix:
    def __init__(self, app, verification=False):
        self.app = app
        self.verification = verification

    async def __call__(self, scope, receive, send):
        prefix = scope.get('root_path', '').rstrip('/')
        path = scope.get('path', '')
        if scope['type'] in ('http','websocket') and prefix and not (path == prefix or path.startswith(prefix+'/')):
            scope=dict(scope,path=prefix+path)
            if 'raw_path' in scope: scope['raw_path']=prefix.encode()+scope['raw_path']
        if self.verification and scope['type']=='http' and scope['method'] not in ('GET','HEAD','OPTIONS'):
            await JSONResponse({'detail':'Upgrade verification mode is read-only'},503)(scope,receive,send)
            return
        await self.app(scope,receive,send)


class SharedAdminSession:
    def __init__(self, app, token, prefix, public_base):
        self.app,self.token,self.prefix=app,token,prefix.rstrip('/')
        url=urlsplit(public_base)
        self.public_origin=f'{url.scheme}://{url.netloc}'

    async def __call__(self,scope,receive,send):
        from . import main as child
        if scope['type']!='http':
            return await self.app(scope,receive,send)
        request=Request(scope)
        path=scope['path']
        if self.prefix and path.startswith(self.prefix+'/'): path=path[len(self.prefix):]
        valid=child.session_is_valid(request.cookies.get('childcare_admin_session'))
        admin=request.headers.get('authorization','')=='Bearer '+self.token
        if valid and scope['method'] not in ('GET','HEAD','OPTIONS'):
            origin=request.headers.get('origin')
            if origin and origin.rstrip('/') not in (self.public_origin,f"{request.url.scheme}://{request.headers.get('host')}"):
                await JSONResponse({'detail':'cross-origin admin request refused'},403)(scope,receive,send); return
        # Both old administration entry points now lead to the same complete console.
        if path in ('/admin','/admin/','/childcare/admin','/childcare/admin/','/childcare/admin/login') and scope['method']=='GET':
            target=self.prefix+('/console' if valid else '/console/login')
            await RedirectResponse(target,303)(scope,receive,send); return
        if path.startswith('/admin/v1') and valid and not request.headers.get('authorization'):
            scope=dict(scope,headers=[*scope['headers'],(b'authorization',('Bearer '+self.token).encode())])
        if path.startswith('/console/api/') and not (valid or admin):
            await JSONResponse({'detail':'login required'},401)(scope,receive,send); return
        if path=='/console' and not (valid or admin):
            await RedirectResponse(self.prefix+'/console/login',303)(scope,receive,send); return
        if valid:
            scope=dict(scope)
            scope.setdefault('state',{})['shared_admin']=True
        await self.app(scope,receive,send)


class Login(BaseModel):
    key: str


def create_app(backup_settings=None):
    from cloud_backup.main import create_app as backup_factory
    from . import main as child
    if backup_settings is None:
        from cloud_backup.config import Settings
        backup_settings=Settings.from_env()
    if not all((child.DEVICE_TOKEN,child.ADMIN_TOKEN,child.HERMES_TOKEN,child.PUBLIC_READ_TOKEN,child.ADMIN_PASSWORD)):
        raise ValueError('Unified deployment requires all childcare role tokens and admin password')
    if child.DB_PATH.resolve()==backup_settings.database_path.resolve():
        raise ValueError('Childcare and backup databases must be separate')
    # The original backup admin credential is the sole console/admin credential.
    child.ADMIN_TOKEN=backup_settings.admin_token
    child.ADMIN_PASSWORD=backup_settings.admin_token
    backup=backup_factory(backup_settings)
    verification=os.getenv('PERSONAL_SERVICES_VERIFY_ONLY','0')=='1'
    prefix=backup_settings.root_path.rstrip('/')
    phone_https=os.getenv('XIGUA_PHONE_HTTPS_URL','').rstrip('/')
    if phone_https:
        entry=urlsplit(phone_https)
        if entry.scheme!='https' or not entry.hostname or entry.username or entry.password or entry.query or entry.fragment or entry.path!=prefix+'/console':
            raise ValueError('XIGUA_PHONE_HTTPS_URL must be the canonical HTTPS console URL')

    @asynccontextmanager
    async def lifespan(app):
        async with AsyncExitStack() as stack:
            if not verification: await stack.enter_async_context(backup.router.lifespan_context(backup))
            await stack.enter_async_context(child.app.router.lifespan_context(child.app))
            yield

    app=FastAPI(title='Personal services',root_path=prefix,lifespan=lifespan,docs_url=None,redoc_url=None)
    app.add_middleware(SharedAdminSession,token=backup_settings.admin_token,prefix=prefix,public_base=backup_settings.public_base_url)
    app.add_middleware(RestoreProxyPrefix,verification=verification)
    app.state.backup=backup

    def html(name):
        source=(Path(__file__).resolve().parents[1]/'web'/name).read_text()
        source=source.replace('__CONSOLE_BASE_JSON__',json.dumps(prefix))
        source=source.replace('__BLUETOOTH_SCRIPT_URL__',html_escape(prefix+'/console/api/bluetooth.js',quote=True))
        return HTMLResponse(source,headers={'Cache-Control':'no-store'})

    @app.get('/console/login')
    def login_page(request:Request):
        if phone_https and request.url.scheme!='https': return RedirectResponse(phone_https+'/login',303)
        return html('login.html')

    @app.post('/console/login')
    def login(payload: Login,response: Response):
        if not hmac.compare_digest(payload.key,backup_settings.admin_token): raise HTTPException(401,'管理 Key 不正确')
        response.set_cookie('childcare_admin_session',child.session_value(),httponly=True,samesite='lax',
                           secure=child.SESSION_COOKIE_SECURE,max_age=43200,path=prefix or '/')
        return {'ok':True}

    @app.post('/console/logout')
    def logout(response: Response):
        response.delete_cookie('childcare_admin_session',path=prefix or '/')
        response.delete_cookie('childcare_admin_session',path=prefix+'/childcare')
        return {'ok':True}

    @app.get('/console')
    def console(request:Request):
        if phone_https and request.url.scheme!='https': return RedirectResponse(phone_https,303)
        return html('console.html')

    @app.get('/console/api/device-config')
    def device_header():
        from .personal import ids
        c,d=ids()
        data='#pragma once\n'+'\n'.join('#define '+key+' '+json.dumps(value) for key,value in {
            'XIGUA_BACKEND_URL':child.PUBLIC_BASE_URL,'XIGUA_BACKEND_DEVICE_TOKEN':child.DEVICE_TOKEN,
            'XIGUA_BACKEND_CHILD_ID':c,'XIGUA_BACKEND_DEVICE_ID':d}.items())+'\n'
        return Response(data,media_type='text/plain',headers={'Content-Disposition':'attachment; filename="xigua_backend_config_local.h"','Cache-Control':'no-store'})

    @app.get('/console/api/bluetooth.js')
    def bluetooth_client():
        source=(Path(__file__).resolve().parents[1]/'web'/'bluetooth.js').read_text()
        return Response(source,media_type='text/javascript',headers={'Cache-Control':'no-store'})

    @app.get('/console/api/backup-overview')
    def backup_overview():
        db=backup.state.database
        counts={name:db.query_one('SELECT COUNT(*) AS n FROM '+name)['n'] for name in ('files','versions')}
        archives=[]
        for path in sorted((backup_settings.data_dir/'backups').glob('bak_*.tar.gz'),reverse=True):
            archives.append({'id':path.name[:-7],'size_bytes':path.stat().st_size})
        return {**counts,'backups':archives}

    @app.get('/healthz')
    def health(): return {'status':'ok','service':'personal-services'}

    @app.get('/readyz')
    def ready():
        try:
            backup.state.database.query_one('SELECT 1')
            with child.get_db() as db: db.execute('SELECT 1 FROM children LIMIT 1')
        except Exception as exc: raise HTTPException(503,'storage unavailable') from exc
        return {'status':'ready','verification_read_only':verification}

    app.mount('/childcare',child.app)
    app.mount('/',backup)
    return app

//! The Unit Builder commands behind tauri's mock runtime: the same handlers and managed state as main.rs, called
//! through the real IPC path (argument names, camelCase, serde). Used by tests/builder_cmds.rs and
//! examples/ui_bridge.rs (the headless UI check).
#![allow(dead_code)]
use ds_unit_builder::tauri_cmds::*;
use serde_json::Value;
use std::path::PathBuf;
use tauri::test::{get_ipc_response, mock_builder, mock_context, noop_assets, MockRuntime, INVOKE_KEY};
use tauri::{App, WebviewWindow};

pub struct Ipc {
    _app: App<MockRuntime>,
    pub webview: WebviewWindow<MockRuntime>,
}

pub fn mock(root: PathBuf) -> Ipc {
    let app = mock_builder()
        .manage(UnitBuilderState::new(root))
        .invoke_handler(tauri::generate_handler![
            builder_catalog, builder_new_unit, builder_compute, builder_validate,
            builder_set_item, builder_set_stack_level, builder_decode_link, builder_encode_link,
            units_list, units_save, units_rename, units_duplicate, units_delete, units_import_link,
            teams_create, teams_update, teams_delete, units_export
        ])
        .build(mock_context(noop_assets()))
        .expect("mock app");
    let webview = tauri::WebviewWindowBuilder::new(&app, "main", Default::default()).build().expect("mock webview");
    Ipc { _app: app, webview }
}

impl Ipc {
    /// one command, args as the UI sends them (camelCase keys); Err = the command's error value
    pub fn invoke(&self, cmd: &str, args: Value) -> Result<Value, Value> {
        let req = tauri::webview::InvokeRequest {
            cmd: cmd.into(),
            callback: tauri::ipc::CallbackFn(0),
            error: tauri::ipc::CallbackFn(1),
            url: "tauri://localhost".parse().unwrap(),
            body: tauri::ipc::InvokeBody::Json(args),
            headers: Default::default(),
            invoke_key: INVOKE_KEY.to_string(),
        };
        get_ipc_response(&self.webview, req).map(|b| b.deserialize::<Value>().unwrap_or(Value::Null))
    }
}

use wow_addon_sdk::*;
fn on_load() -> i32 {
    let _ = subscribe("PLAYER_LOGIN");
    let _ = print("Hello from WASM");
    0
}
fn on_event(event: &str) -> i32 {
    if event == "PLAYER_LOGIN" { print("Hello from WASM (PLAYER_LOGIN)") } else { 0 }
}
wow_addon!(on_load, on_event);

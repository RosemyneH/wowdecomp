use wow_addon_sdk::*;
fn on_load() -> i32 {
    let r = subscribe("BAG_UPDATE");
    if r != 0 { return r; }
    report()
}
fn report() -> i32 {
    match inventory() {
        Ok(items) => {
            let total: u32 = items.iter().map(|i| i.count).sum();
            print(&format!("Inventory: {} stacks, {} items", items.len(), total))
        }
        Err(code) => print(&format!("Inventory unavailable: {}", code)),
    }
}
fn on_event(event: &str) -> i32 { if event == "BAG_UPDATE" { report() } else { 0 } }
wow_addon!(on_load, on_event);

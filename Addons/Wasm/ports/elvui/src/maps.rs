use wow_addon_sdk::lua;

pub fn enable(parent: i32) -> Result<(), i32> {
    if let Ok(lua::Val::Obj(cluster)) = lua::global("MinimapCluster") {
        lua::set_point(cluster, "TOPRIGHT", parent, "TOPRIGHT", -4.0, -4.0)?;
        lua::set_size(cluster, 180.0, 180.0)?;
    }
    Ok(())
}

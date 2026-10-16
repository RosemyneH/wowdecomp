use wow_addon_sdk::lua::{self, Val};

pub fn enable(parent: i32) -> Result<(), i32> {
    for name in ["ContainerFrame1", "ContainerFrame2", "ContainerFrame3", "ContainerFrame4", "ContainerFrame5"] {
        let Ok(Val::Obj(frame)) = lua::global(name) else { continue };
        let _ = lua::set_point(frame, "BOTTOMRIGHT", parent, "BOTTOMRIGHT", -8.0, 80.0);
    }
    Ok(())
}

use cardputer_voice::settings;
use std::io::{self, IsTerminal};

fn run() -> io::Result<()> {
    let path = settings::key_path()?;
    let action=std::env::args().nth(1);
    match action.as_deref() {
        Some("status") => println!("{}", settings::status(&path)?),
        Some("select-deepseek") => settings::select_provider("deepseek")?,
        Some("select-openai") => settings::select_provider("openai")?,
        Some("save-key") | Some("save-key-deepseek") | Some("save-key-openai") => {
            if io::stdin().is_terminal() {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "Key must be supplied through a protected input pipe"));
            }
            let target=match action.as_deref() {
                Some("save-key-deepseek")=>settings::provider_key_path("deepseek")?,
                Some("save-key-openai")=>settings::provider_key_path("openai")?,
                _=>path.clone(),
            };
            settings::save_key(&target, io::stdin().lock())?;
            println!("Key saved");
        }
        Some("enable") => { settings::set_enabled(true, &path)?; println!("Service enabled"); }
        Some("disable") => { settings::set_enabled(false, &path)?; println!("Service disabled"); }
        _ => return Err(io::Error::new(io::ErrorKind::InvalidInput, "Usage: settings status|save-key|enable|disable")),
    }
    Ok(())
}
fn main() { if let Err(error) = run() { eprintln!("{error}"); std::process::exit(1); } }

# Prepared fixture diagnostic only; not installed or executed by preflight.
# Replace only the disposable public fixture's Qeynos2 Rat quest. Log raw
# server coordinates and Mob size so LoS eye-height adjustments are explicit.
sub EVENT_SPAWN {
    quest::settimer("client_collision_ab", 1);
}

sub EVENT_TIMER {
    return unless $timer eq "client_collision_ab";
    if (-e "/runtime/stop-client-collision-ab") {
        quest::stoptimer("client_collision_ab");
        quest::debug("CLIENT_AB_PROBE_STOPPED");
        return;
    }
    my @targets = ([-935, 1194, 0], [-925, 1194, 0]);
    for my $i (0 .. $#targets) {
        my ($x, $y, $z) = @{$targets[$i]};
        my $ground = $npc->FindGroundZ($x, $y, 100);
        my $los = $npc->CheckLoSToLoc($x, $y, $z, 6.0);
        quest::debug("CLIENT_AB_QUERY id=$i time=".time().
            " from=".$npc->GetX().",".$npc->GetY().",".$npc->GetZ().
            " size=".$npc->GetSize()." to=$x,$y,$z target_size=6 ground=$ground los=$los");
    }
}

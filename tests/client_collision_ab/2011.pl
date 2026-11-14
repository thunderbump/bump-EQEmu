# Task-owned stationary diagnostic. Actual production Perl bindings, never client perception.
use Time::HiRes qw(time);
sub EVENT_SPAWN { quest::settimer("client_collision_ab", 1); }
sub EVENT_TIMER {
    return unless $timer eq "client_collision_ab";
    if (-e "/runtime/stop-client-collision-ab") {
        quest::stoptimer("client_collision_ab");
        quest::debug("CLIENT_AB_PROBE_STOPPED");
        return;
    }
    # Freeze model origin each observation, bypassing NPC idle ground adjustment.
    $npc->GMMove(297.75, 344, 3.75, 0, 0);
    my @targets = (["clear",288,344,3.75], ["blocked",320,344,3.75]);
    for my $t (@targets) {
        my ($id,$x,$y,$z) = @$t;
        my $ground = $npc->FindGroundZ($x,$y,100);
        my $los = $npc->CheckLoSToLoc($x,$y,$z,6.0);
        my $eye = unpack("f",pack("f",$npc->GetZ()+unpack("f",pack("f",$npc->GetSize()/2*unpack("f",pack("f",0.9))))));
        my $target_eye = $z+6/2*0.5;
        quest::debug("CLIENT_AB_QUERY id=$id time=".time()." from=".$npc->GetX().",".$npc->GetY().",".$npc->GetZ()." size=".$npc->GetSize()." eyes=297.75,344,$eye to=$x,$y,$z target_size=6 target_eyes=$x,$y,$target_eye ground=$ground los=$los");
    }
    my $c = $entity_list->GetClientByName("Clientab");
    if ($c) {
        quest::debug("CLIENT_AB_POSE time=".time()." name=Clientab entity=".$c->GetID()." account=".$c->AccountID()." character=".$c->CharacterID()." aggro=".$c->GetAggroCount()." pose=".$c->GetX().",".$c->GetY().",".$c->GetZ());
    }
}

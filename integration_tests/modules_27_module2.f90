module tomlf_build_array
   use tomlf_build_keyval, only : get_value
   implicit none

   interface get_value
      module procedure :: get_elem_table
      module procedure :: get_elem_array
      module procedure :: get_elem_keyval
   end interface get_value


contains


subroutine get_elem_table(array, pos, ptr, stat)
   integer, intent(inout) :: array
   integer, intent(in) :: pos
   real, pointer, intent(out) :: ptr
   integer, intent(out), optional :: stat

end subroutine get_elem_table


subroutine get_elem_array(array, pos, ptr, stat)
   complex, intent(inout) :: array
   integer, intent(in) :: pos
   real, pointer, intent(out) :: ptr
   integer, intent(out), optional :: stat

end subroutine get_elem_array


subroutine get_elem_keyval(array, pos, ptr, stat)
   integer, intent(inout) :: array
   integer, intent(in) :: pos
   complex, pointer, intent(out) :: ptr
   integer, intent(out), optional :: stat

end subroutine get_elem_keyval

end module tomlf_build_array
